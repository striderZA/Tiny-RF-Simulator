#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include "agent_catalog.h"
#include "agent_errors.h"
#include "agent_host.h"
#include "agent_server.h"
#include "agent_socket.h"
#include "agent_wire.h"
#include "agent_wire_test_support.h"
#include "test_temp_paths.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace {

class ScratchDirectory {
  public:
    ScratchDirectory() {
        static std::atomic<unsigned long long> next{0};
        m_path = std::filesystem::temp_directory_path() /
                 ("rfsim-agent-server-" + test_temp_paths::processTag() + "-" +
                  std::to_string(next.fetch_add(1)));
        std::filesystem::create_directories(m_path);
    }
    ~ScratchDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }
    std::filesystem::path endpoint() const { return m_path / "private" / "agent-endpoint.json"; }

  private:
    std::filesystem::path m_path;
};

class TestHost final : public IAgentHost {
  public:
    void beginCheckpoint(const AgentCall &) override {}
    void commitCheckpoint(const std::string &) override {}
    void discardCheckpoint() override {}
    void placeComponents(const std::vector<AgentPlacement> &) override {}
    bool appModalOpen() const override { return modal; }
    std::optional<std::string> projectName() const override { return std::nullopt; }
    void recordActivity(const AgentActivity &) override {}
    bool modal = false;
};

class TestExecutor final : public IAgentCallExecutor {
  public:
    AgentToolResult execute(const AgentCall &call) override {
        calls.push_back(call.tool);
        if (on_execute)
            on_execute();
        if (result_override)
            return *result_override;
        return {false, nlohmann::json{{"tool", call.tool}, {"client", call.client}}};
    }
    std::uint64_t epoch() const override { return current_epoch; }
    std::vector<std::string> calls;
    std::function<void()> on_execute;
    std::uint64_t current_epoch = 41;
    std::optional<AgentToolResult> result_override;
};

AgentServerConfig configFor(const ScratchDirectory &scratch) {
    AgentServerConfig config;
    config.endpoint_file = scratch.endpoint();
    config.app_version = "9.8.7";
    return config;
}

bool waitFor(const std::function<bool()> &predicate, std::chrono::milliseconds timeout = 2s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate())
            return true;
        std::this_thread::sleep_for(5ms);
    }
    return predicate();
}

std::optional<nlohmann::json> receiveUntil(TestBridgeClient &client,
                                           std::chrono::milliseconds timeout = 2s) {
    return client.receive(timeout);
}

} // namespace

TEST_CASE("start writes an endpoint and stop removes it", "[agent_server]") {
    ScratchDirectory scratch;
    TestExecutor executor;
    TestHost host;
    AgentServer server(executor, host, configFor(scratch));
    std::string error;

    REQUIRE(server.start(&error));
    REQUIRE(std::filesystem::exists(scratch.endpoint()));
    const auto endpoint = readAgentEndpoint(scratch.endpoint(), &error);
    REQUIRE(endpoint.has_value());
    CHECK(endpoint->port > 0);
    CHECK(endpoint->app_version == "9.8.7");
    CHECK(endpoint->catalog_version == kAgentCatalogVersion);
    CHECK(endpoint->bridge_token != endpoint->gui_token);

    server.stop();
    CHECK_FALSE(std::filesystem::exists(scratch.endpoint()));
    CHECK_FALSE(server.running());
}

TEST_CASE("stop leaves a replacement endpoint owned by another token", "[agent_server]") {
    ScratchDirectory scratch;
    TestExecutor executor;
    TestHost host;
    AgentServer server(executor, host, configFor(scratch));
    std::string error;
    REQUIRE(server.start(&error));
    auto endpoint = readAgentEndpoint(scratch.endpoint(), &error);
    REQUIRE(endpoint.has_value());

    endpoint->gui_token =
        endpoint->gui_token == std::string(64, 'a') ? std::string(64, 'b') : std::string(64, 'a');
    REQUIRE(writeAgentEndpoint(scratch.endpoint(), *endpoint, &error));
    server.stop();

    CHECK(std::filesystem::exists(scratch.endpoint()));
    const auto replacement = readAgentEndpoint(scratch.endpoint(), &error);
    REQUIRE(replacement.has_value());
    CHECK(replacement->gui_token == endpoint->gui_token);
}

TEST_CASE("bad first lines close silently", "[agent_server]") {
    SECTION("wrong bridge token") {
        ScratchDirectory scratch;
        TestExecutor executor;
        TestHost host;
        AgentServer server(executor, host, configFor(scratch));
        std::string error;
        REQUIRE(server.start(&error));
        auto client = TestBridgeClient::connect(scratch.endpoint(), &error);
        REQUIRE(client.has_value());
        REQUIRE(
            client->sendRawLine(nlohmann::json{{"jsonrpc", "2.0"},
                                               {"id", 1},
                                               {"method", "hello"},
                                               {"params",
                                                {{"protocol", kAgentWireProtocol},
                                                 {"bridge_token", std::string(64, '0')},
                                                 {"catalog_version", kAgentCatalogVersion},
                                                 {"client", {{"name", "bad"}, {"version", "1"}}}}}}
                                    .dump()));
        CHECK(client->readStatus(error, 2s) == AgentReadStatus::Closed);
        server.stop();
    }
    SECTION("HTTP request line") {
        ScratchDirectory scratch;
        TestExecutor executor;
        TestHost host;
        AgentServer server(executor, host, configFor(scratch));
        std::string error;
        REQUIRE(server.start(&error));
        auto client = TestBridgeClient::connect(scratch.endpoint(), &error);
        REQUIRE(client.has_value());
        REQUIRE(client->sendRawLine("GET / HTTP/1.1"));
        CHECK(client->readStatus(error, 2s) == AgentReadStatus::Closed);
        server.stop();
    }
    SECTION("oversized frame") {
        ScratchDirectory scratch;
        TestExecutor executor;
        TestHost host;
        AgentServer server(executor, host, configFor(scratch));
        std::string error;
        REQUIRE(server.start(&error));
        auto client = TestBridgeClient::connect(scratch.endpoint(), &error);
        REQUIRE(client.has_value());
        REQUIRE(client->sendRawLine(std::string(kAgentMaxLineBytes + 1, 'x')));
        CHECK(client->readStatus(error, 5s) == AgentReadStatus::Closed);
        server.stop();
    }
}

TEST_CASE("a second connected client receives BUSY and closes", "[agent_server]") {
    ScratchDirectory scratch;
    TestExecutor executor;
    TestHost host;
    AgentServer server(executor, host, configFor(scratch));
    std::string error;
    REQUIRE(server.start(&error));
    auto first = TestBridgeClient::connect(scratch.endpoint(), &error);
    REQUIRE(first.has_value());
    REQUIRE(first->hello("first"));
    auto second = TestBridgeClient::connect(scratch.endpoint(), &error);
    REQUIRE(second.has_value());
    const auto response = receiveUntil(*second);
    REQUIRE(response.has_value());
    REQUIRE(response->contains("error"));
    CHECK(response->at("error").at("code") == kAgentRpcErrorCode);
    CHECK(response->at("error").at("data").at("code") == "BUSY");
    CHECK(response->at("error").at("message") == "another agent is connected");
    CHECK(second->readStatus(error, 2s) == AgentReadStatus::Closed);
    server.stop();
}

TEST_CASE("catalog mismatch is answered and then closed", "[agent_server]") {
    ScratchDirectory scratch;
    TestExecutor executor;
    TestHost host;
    AgentServer server(executor, host, configFor(scratch));
    std::string error;
    REQUIRE(server.start(&error));
    auto client = TestBridgeClient::connect(scratch.endpoint(), &error);
    REQUIRE(client.has_value());
    REQUIRE(client->sendRawLine(nlohmann::json{{"jsonrpc", "2.0"},
                                               {"id", 1},
                                               {"method", "hello"},
                                               {"params",
                                                {{"protocol", kAgentWireProtocol},
                                                 {"bridge_token", client->endpoint().bridge_token},
                                                 {"catalog_version", kAgentCatalogVersion + 1},
                                                 {"client", {{"name", "test"}, {"version", "1"}}}}}}
                                    .dump()));
    const auto response = receiveUntil(*client);
    REQUIRE(response.has_value());
    REQUIRE(response->contains("error"));
    CHECK(response->at("error").at("code") == kAgentRpcErrorCode);
    CHECK(response->at("error").at("data").at("code") == "VERSION_MISMATCH");
    CHECK(client->readStatus(error, 2s) == AgentReadStatus::Closed);
    server.stop();
}

TEST_CASE("a client that never says hello is closed after the configured timeout",
          "[agent_server]") {
    ScratchDirectory scratch;
    TestExecutor executor;
    TestHost host;
    auto config = configFor(scratch);
    config.hello_timeout = 100ms;
    AgentServer server(executor, host, std::move(config));
    std::string error;
    REQUIRE(server.start(&error));
    auto client = TestBridgeClient::connect(scratch.endpoint(), &error);
    REQUIRE(client.has_value());
    CHECK(client->readStatus(error, 2s) == AgentReadStatus::Closed);
    server.stop();
}

TEST_CASE("calls round-trip through pump with connected client status", "[agent_server]") {
    ScratchDirectory scratch;
    TestExecutor executor;
    TestHost host;
    AgentServer server(executor, host, configFor(scratch));
    std::string error;
    REQUIRE(server.start(&error));
    server.pump();
    auto client = TestBridgeClient::connect(scratch.endpoint(), &error);
    REQUIRE(client.has_value());
    REQUIRE(client->hello("round-trip-client", "2.3"));
    const auto hello = client->lastResponse();
    REQUIRE(hello.has_value());
    CHECK(hello->at("result").at("gui_token") == client->guiToken());
    CHECK(hello->at("result").at("app_version") == "9.8.7");
    CHECK(hello->at("result").at("catalog_version") == kAgentCatalogVersion);
    CHECK(hello->at("result").at("epoch") == executor.epoch());
    REQUIRE(waitFor([&] { return server.status().state == AgentServerState::Connected; }));
    CHECK(server.status().client_name == "round-trip-client");
    CHECK(server.status().client_version == "2.3");

    REQUIRE(client->call("test_tool", {{"value", 5}}));
    std::this_thread::sleep_for(30ms);
    server.pump();
    const auto reply = receiveUntil(*client);
    REQUIRE(reply.has_value());
    REQUIRE(reply->contains("result"));
    CHECK(reply->at("result").at("is_error") == false);
    CHECK((reply->at("result").at("structured") ==
           nlohmann::json{{"tool", "test_tool"}, {"client", "round-trip-client"}}));
    CHECK(reply->at("result").at("text") == reply->at("result").at("structured").dump());
    server.stop();
}

TEST_CASE("pump preserves arrival order and enforces budget while always progressing",
          "[agent_server]") {
    ScratchDirectory scratch;
    TestExecutor executor;
    TestHost host;
    auto now = std::chrono::steady_clock::time_point{};
    auto config = configFor(scratch);
    config.pump_budget = 8ms;
    config.now = [&] { return now; };
    executor.on_execute = [&] { now += 5ms; };
    AgentServer server(executor, host, config);
    std::string error;
    REQUIRE(server.start(&error));
    auto client = TestBridgeClient::connect(scratch.endpoint(), &error);
    REQUIRE(client.has_value());
    REQUIRE(client->hello());
    for (const auto *name : {"first", "second", "third"})
        REQUIRE(client->call(name));
    REQUIRE(client->sendRawLine(nlohmann::json{
        {"jsonrpc", "2.0"}, {"id", 99}, {"method", "call"}, {"params", nlohmann::json::object()}}
                                    .dump()));
    const auto barrier = receiveUntil(*client);
    REQUIRE(barrier.has_value());
    REQUIRE(barrier->contains("error"));
    CHECK(barrier->at("error").at("data").at("code") == "INVALID_ARGUMENT");

    server.pump();
    CHECK((executor.calls == std::vector<std::string>{"first", "second"}));
    CHECK(server.status().state == AgentServerState::Busy);
    server.stop();

    ScratchDirectory short_budget_scratch;
    TestExecutor short_budget_executor;
    TestHost short_budget_host;
    auto short_now = std::chrono::steady_clock::time_point{};
    auto short_config = configFor(short_budget_scratch);
    short_config.pump_budget = 1ms;
    short_config.now = [&] { return short_now; };
    short_budget_executor.on_execute = [&] { short_now += 5ms; };
    AgentServer short_budget_server(short_budget_executor, short_budget_host, short_config);
    REQUIRE(short_budget_server.start(&error));
    auto short_client = TestBridgeClient::connect(short_budget_scratch.endpoint(), &error);
    REQUIRE(short_client.has_value());
    REQUIRE(short_client->hello());
    REQUIRE(short_client->call("over-budget"));
    std::this_thread::sleep_for(30ms);
    short_budget_server.pump();
    CHECK((short_budget_executor.calls == std::vector<std::string>{"over-budget"}));
    short_budget_server.stop();
}

TEST_CASE("modal calls park and time out with the exact BUSY response", "[agent_server]") {
    ScratchDirectory scratch;
    TestExecutor executor;
    TestHost host;
    auto now = std::chrono::steady_clock::time_point{};
    auto config = configFor(scratch);
    config.park_timeout = 30s;
    config.now = [&] { return now; };
    host.modal = true;
    AgentServer server(executor, host, config);
    std::string error;
    REQUIRE(server.start(&error));
    auto client = TestBridgeClient::connect(scratch.endpoint(), &error);
    REQUIRE(client.has_value());
    REQUIRE(client->hello());
    REQUIRE(client->call("parked"));
    std::this_thread::sleep_for(30ms);

    server.pump();
    CHECK(executor.calls.empty());
    now += 31s;
    server.pump();
    const auto response = receiveUntil(*client);
    REQUIRE(response.has_value());
    REQUIRE(response->contains("result"));
    CHECK(response->at("result").at("is_error") == true);
    CHECK(response->at("result").at("structured").at("error").at("code") == "BUSY");
    CHECK(response->at("result").at("structured").at("error").at("message") ==
          "RF Simulator is showing a dialog; ask the user to close it, then retry");
    CHECK(response->at("result").at("text") == response->at("result").at("structured").dump());
    server.stop();
}

TEST_CASE("stop fails queued calls with SIMULATOR_UNAVAILABLE", "[agent_server]") {
    ScratchDirectory scratch;
    TestExecutor executor;
    TestHost host;
    AgentServer server(executor, host, configFor(scratch));
    std::string error;
    REQUIRE(server.start(&error));
    auto client = TestBridgeClient::connect(scratch.endpoint(), &error);
    REQUIRE(client.has_value());
    REQUIRE(client->hello());
    REQUIRE(client->call("queued"));
    std::this_thread::sleep_for(30ms);

    server.stop();
    const auto response = receiveUntil(*client);
    REQUIRE(response.has_value());
    REQUIRE(response->contains("result"));
    CHECK(response->at("result").at("is_error") == true);
    CHECK(response->at("result").at("structured").at("error").at("code") ==
          "SIMULATOR_UNAVAILABLE");
}

TEST_CASE("oversized tool results are replaced with a bounded internal error", "[agent_server]") {
    ScratchDirectory scratch;
    TestExecutor executor;
    executor.result_override =
        AgentToolResult{false, nlohmann::json{{"payload", std::string(kAgentMaxLineBytes, 'x')}}};
    TestHost host;
    AgentServer server(executor, host, configFor(scratch));
    std::string error;
    REQUIRE(server.start(&error));
    auto client = TestBridgeClient::connect(scratch.endpoint(), &error);
    REQUIRE(client.has_value());
    REQUIRE(client->hello());
    REQUIRE(client->call("oversized"));
    std::this_thread::sleep_for(30ms);

    server.pump();
    const auto response = receiveUntil(*client);
    REQUIRE(response.has_value());
    REQUIRE(response->contains("result"));
    const auto &result = response->at("result");
    CHECK(result.at("is_error") == true);
    const auto &structured = result.at("structured");
    const AgentError expected_error{AgentErrorCode::Internal,
                                    "Agent tool result exceeds the 1 MiB protocol limit"};
    CHECK(structured == agentErrorResult(expected_error, executor.epoch()).structured);
    CHECK(result.at("text") == structured.dump());
    CHECK(response->dump().size() <= kAgentMaxLineBytes);
    server.stop();
}

namespace {
// Pumps until the client has a reply, so the test waits on the response instead of a fixed delay.
std::optional<nlohmann::json> pumpUntilReply(AgentServer &server, TestBridgeClient &client,
                                             std::chrono::milliseconds timeout = 2s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        server.pump();
        if (auto reply = client.receive(50ms))
            return reply;
    }
    return std::nullopt;
}
} // namespace

TEST_CASE("Calls nested beyond the depth limit are refused and the server keeps serving",
          "[agent_server]") {
    ScratchDirectory scratch;
    TestExecutor executor;
    TestHost host;
    AgentServer server(executor, host, configFor(scratch));
    std::string error;
    REQUIRE(server.start(&error));
    server.pump();
    auto client = TestBridgeClient::connect(scratch.endpoint(), &error);
    REQUIRE(client.has_value());
    REQUIRE(client->hello("deep-client", "1.0"));
    REQUIRE(waitFor([&] { return server.status().state == AgentServerState::Connected; }));

    nlohmann::json deep = nlohmann::json::array();
    for (std::size_t level = 1; level < 70; ++level) {
        nlohmann::json outer = nlohmann::json::array();
        outer.push_back(std::move(deep));
        deep = std::move(outer);
    }
    REQUIRE(client->call("test_tool", nlohmann::json{{"deep", std::move(deep)}}));
    const auto rejected = pumpUntilReply(server, *client);
    REQUIRE(rejected.has_value());
    REQUIRE(rejected->contains("error"));
    CHECK(rejected->at("error").at("data").at("code") == "INVALID_ARGUMENT");

    REQUIRE(client->call("test_tool", {{"value", 5}}));
    const auto reply = pumpUntilReply(server, *client);
    REQUIRE(reply.has_value());
    REQUIRE(reply->contains("result"));
    CHECK(reply->at("result").at("is_error") == false);
    server.stop();
}
