#if __has_include("circuit_runtime.h") || __has_include("editor_commands.h") ||                    \
                                                        __has_include("imgui.h")
#error "mcp_adapter must link no simulator code"
#endif

#include "agent_catalog.h"
#include "agent_errors.h"
#include "gui_link.h"
#include "mcp_session.h"

#include "../agent/mcp/src/mcp_bridge_detail.h"
#include "mcp_bridge.h"

#include "agent_endpoint.h"
#include "agent_socket.h"
#include "agent_wire.h"
#include "fake_gui.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <sstream>
#include <streambuf>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

constexpr std::string_view kModernVersion = "2026-07-28";
constexpr std::string_view kProtocolVersionMeta = "io.modelcontextprotocol/protocolVersion";
constexpr std::string_view kClientCapabilitiesMeta = "io.modelcontextprotocol/clientCapabilities";
constexpr std::string_view kClientInfoMeta = "io.modelcontextprotocol/clientInfo";
constexpr std::string_view kServerInfoMeta = "io.modelcontextprotocol/serverInfo";

McpServerIdentity testIdentity() {
    McpServerIdentity identity;
    identity.name = "test-rf-mcp";
    identity.title = "Test RF Simulator";
    identity.version = "3.2.1";
    return identity;
}

OrderedJson rpcRequest(OrderedJson id, std::string method,
                       OrderedJson params = OrderedJson::object()) {
    return OrderedJson{{"jsonrpc", "2.0"},
                       {"id", std::move(id)},
                       {"method", std::move(method)},
                       {"params", std::move(params)}};
}

OrderedJson modernParams(OrderedJson params = OrderedJson::object(),
                         std::string protocol_version = std::string(kModernVersion),
                         bool include_client_capabilities = true,
                         std::optional<OrderedJson> client_info = std::nullopt) {
    OrderedJson metadata = OrderedJson::object();
    metadata[std::string(kProtocolVersionMeta)] = std::move(protocol_version);
    if (include_client_capabilities) {
        metadata[std::string(kClientCapabilitiesMeta)] = OrderedJson::object();
    }
    if (client_info.has_value()) {
        metadata[std::string(kClientInfoMeta)] = std::move(*client_info);
    }
    params["_meta"] = std::move(metadata);
    return params;
}

McpStep initializeLegacy(McpSession &session, std::string protocol_version,
                         OrderedJson id = OrderedJson(1)) {
    OrderedJson params{{"protocolVersion", std::move(protocol_version)},
                       {"capabilities", OrderedJson::object()},
                       {"clientInfo", OrderedJson{{"name", "test-client"}, {"version", "1.0"}}}};
    return session.onLine(rpcRequest(std::move(id), "initialize", std::move(params)).dump());
}

McpStep sendLegacy(McpSession &session, OrderedJson id, std::string method,
                   OrderedJson params = OrderedJson::object()) {
    return session.onLine(rpcRequest(std::move(id), std::move(method), std::move(params)).dump());
}

McpStep sendModern(McpSession &session, OrderedJson id, std::string method,
                   OrderedJson params = OrderedJson::object(),
                   std::string protocol_version = std::string(kModernVersion),
                   bool include_client_capabilities = true,
                   std::optional<OrderedJson> client_info = std::nullopt) {
    return session.onLine(
        rpcRequest(std::move(id), std::move(method),
                   modernParams(std::move(params), std::move(protocol_version),
                                include_client_capabilities, std::move(client_info)))
            .dump());
}

const Json &rpcError(const McpStep &step) { return step.replies.at(0).at("error"); }

void checkModernServerInfo(const Json &result) {
    CHECK(result.at("resultType") == "complete");
    const auto &server_info = result.at("_meta").at(std::string(kServerInfoMeta));
    CHECK(server_info.at("name") == "test-rf-mcp");
    CHECK(server_info.at("title") == "Test RF Simulator");
    CHECK(server_info.at("version") == "3.2.1");
}

struct BridgeRun {
    int status;
    std::string stdout_text;
    std::string stderr_text;
    std::vector<Json> messages;
};

class BlockingInputBuffer final : public std::streambuf {
  public:
    void append(std::string_view bytes) {
        {
            std::lock_guard lock(m_mutex);
            m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
        }
        m_ready.notify_all();
    }

    void closeInput() {
        {
            std::lock_guard lock(m_mutex);
            m_closed = true;
        }
        m_ready.notify_all();
    }

  protected:
    int_type underflow() override {
        std::unique_lock lock(m_mutex);
        m_ready.wait(lock, [this] { return !m_bytes.empty() || m_closed; });
        if (m_bytes.empty())
            return traits_type::eof();
        return traits_type::to_int_type(m_bytes.front());
    }

    int_type uflow() override {
        std::unique_lock lock(m_mutex);
        m_ready.wait(lock, [this] { return !m_bytes.empty() || m_closed; });
        if (m_bytes.empty())
            return traits_type::eof();
        const char byte = m_bytes.front();
        m_bytes.pop_front();
        return traits_type::to_int_type(byte);
    }

  private:
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::deque<char> m_bytes;
    bool m_closed = false;
};

class FlushObservingBuffer final : public std::streambuf {
  public:
    bool waitForResponse(const Json &id, std::chrono::milliseconds timeout) {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout, [this, &id] { return hasResponseLocked(id); });
    }

    std::string snapshot() const {
        std::lock_guard lock(m_mutex);
        return m_text;
    }

    std::size_t flushCount() const {
        std::lock_guard lock(m_mutex);
        return m_flush_count;
    }

  protected:
    std::streamsize xsputn(const char *bytes, std::streamsize count) override {
        std::lock_guard lock(m_mutex);
        m_text.append(bytes, static_cast<std::size_t>(count));
        return count;
    }

    int_type overflow(int_type ch) override {
        if (traits_type::eq_int_type(ch, traits_type::eof()))
            return traits_type::not_eof(ch);
        {
            std::lock_guard lock(m_mutex);
            m_text.push_back(traits_type::to_char_type(ch));
        }
        return ch;
    }

    int sync() override {
        {
            std::lock_guard lock(m_mutex);
            m_flushed_text = m_text;
            ++m_flush_count;
        }
        m_changed.notify_all();
        return 0;
    }

  private:
    bool hasResponseLocked(const Json &id) const {
        std::size_t start = 0;
        while (true) {
            const auto end = m_flushed_text.find('\n', start);
            if (end == std::string::npos)
                return false;
            if (end != start) {
                try {
                    const auto message = Json::parse(m_flushed_text.substr(start, end - start));
                    if (message.value("id", Json()) == id)
                        return true;
                } catch (...) {
                }
            }
            start = end + 1;
        }
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::string m_text;
    std::string m_flushed_text;
    std::size_t m_flush_count = 0;
};

std::vector<Json> parseBridgeOutput(std::string_view text) {
    std::istringstream lines{std::string(text)};
    std::vector<Json> messages;
    std::string line;
    while (std::getline(lines, line))
        messages.push_back(Json::parse(line));
    return messages;
}

BridgeRun invokeBridge(std::string input, BridgeOptions options) {
    std::istringstream in(std::move(input));
    std::ostringstream out, err;
    const int status = runBridge(in, out, err, options);
    const auto stdout_text = out.str();
    return {status, stdout_text, err.str(), parseBridgeOutput(stdout_text)};
}

BridgeOptions bridgeOptions(std::optional<std::filesystem::path> endpoint_file = std::nullopt) {
    BridgeOptions options;
    options.endpoint_file = std::move(endpoint_file);
    options.exe_dir = std::filesystem::current_path();
    options.server_version = "3.2.1";
    options.timeouts.connect = 200ms;
    options.timeouts.hello = 500ms;
    options.timeouts.call = 1s;
    return options;
}

void appendBridgeLine(std::string &input, const OrderedJson &message,
                      std::string_view newline = "\n") {
    input += message.dump();
    input += newline;
}

OrderedJson modernBridgeRequest(OrderedJson id, std::string method,
                                OrderedJson params = OrderedJson::object(),
                                std::optional<OrderedJson> client_info = std::nullopt) {
    return rpcRequest(
        std::move(id), std::move(method),
        modernParams(std::move(params), std::string(kModernVersion), true, std::move(client_info)));
}

const Json *bridgeResponse(const BridgeRun &run, const Json &id) {
    for (const auto &message : run.messages) {
        if (message.value("id", Json()) == id)
            return &message;
    }
    return nullptr;
}

OrderedJson bridgeCallParams(std::string query) {
    return OrderedJson{{"name", "library_search"},
                       {"arguments", OrderedJson{{"query", std::move(query)}}}};
}

TEST_CASE("A transcript per protocol version runs against a fake GUI", "[bridge]") {
    const auto run_transcript = [](std::string protocol_version, bool modern) {
        FakeGui gui;
        auto gui_call_count = std::make_shared<std::atomic<unsigned>>(0);
        gui.setScript([gui_call_count](const Json &request) -> std::optional<FakeGui::Reply> {
            const auto call_index = gui_call_count->fetch_add(1);
            if (call_index == 0) {
                return FakeGui::Reply{Json{{"jsonrpc", "2.0"},
                                           {"id", request.at("id")},
                                           {"result",
                                            {{"is_error", false},
                                             {"structured", {{"ok", true}}},
                                             {"text", "{\"ok\":true}"}}}}};
            }
            return FakeGui::Reply{Json{{"jsonrpc", "2.0"},
                                       {"id", request.at("id")},
                                       {"error",
                                        {{"code", kAgentRpcErrorCode},
                                         {"message", "bad arguments"},
                                         {"data", {{"code", "INVALID_ARGUMENT"}}}}}}};
        });

        std::string input;
        if (modern) {
            appendBridgeLine(input, modernBridgeRequest(1, "server/discover"));
        } else {
            appendBridgeLine(
                input,
                rpcRequest(1, "initialize",
                           OrderedJson{{"protocolVersion", protocol_version},
                                       {"capabilities", OrderedJson::object()},
                                       {"clientInfo",
                                        {{"name", "transcript-client"}, {"version", "5.0"}}}}));
            appendBridgeLine(
                input, OrderedJson{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
        }

        const OrderedJson client_info{{"name", "transcript-client"}, {"version", "5.0"}};
        const auto append_request = [&](OrderedJson id, std::string method,
                                        OrderedJson params = OrderedJson::object()) {
            if (modern) {
                appendBridgeLine(input, modernBridgeRequest(std::move(id), std::move(method),
                                                            std::move(params), client_info));
            } else {
                appendBridgeLine(input,
                                 rpcRequest(std::move(id), std::move(method), std::move(params)));
            }
        };
        append_request(2, "tools/list");
        append_request(3, "tools/call", bridgeCallParams("success"));
        append_request(4, "tools/call", bridgeCallParams("failure"));

        const auto run = invokeBridge(std::move(input), bridgeOptions(gui.endpointFile()));
        CHECK(run.status == 0);
        CHECK(gui.authenticated());
        CHECK(gui_call_count->load() == 2);

        const auto *handshake = bridgeResponse(run, 1);
        REQUIRE(handshake != nullptr);
        REQUIRE(handshake->contains("result"));
        if (modern) {
            CHECK(handshake->at("result").at("supportedVersions") ==
                  Json::array({std::string(kModernVersion)}));
        } else {
            CHECK(handshake->at("result").at("protocolVersion") == protocol_version);
        }

        const auto *list = bridgeResponse(run, 2);
        REQUIRE(list != nullptr);
        REQUIRE(list->contains("result"));
        CHECK(list->at("result").at("tools").size() == 8);

        const auto *success = bridgeResponse(run, 3);
        REQUIRE(success != nullptr);
        REQUIRE(success->contains("result"));
        REQUIRE(success->at("result").at("content").size() == 1);
        CHECK(success->at("result").at("content")[0].at("text") == "{\"ok\":true}");

        const auto *tool_error = bridgeResponse(run, 4);
        REQUIRE(tool_error != nullptr);
        REQUIRE(tool_error->contains("result"));
        CHECK(tool_error->at("result").at("isError") == true);
    };

    SECTION("2025-06-18") { run_transcript("2025-06-18", false); }
    SECTION("2025-11-25") { run_transcript("2025-11-25", false); }
    SECTION("2026-07-28") { run_transcript(std::string(kModernVersion), true); }
}

TEST_CASE("A cancelled call gets no response and later calls still work", "[bridge]") {
    FakeGui gui;
    std::mutex gui_mutex;
    std::condition_variable gui_changed;
    bool first_call_entered = false;
    bool release_first_call = false;
    std::vector<std::string> gui_call_order;
    gui.setScript([&](const Json &request) -> std::optional<FakeGui::Reply> {
        {
            std::unique_lock lock(gui_mutex);
            gui_call_order.push_back(
                request.at("params").at("arguments").at("query").get<std::string>());
            if (gui_call_order.size() == 1) {
                first_call_entered = true;
                gui_changed.notify_all();
                gui_changed.wait(lock, [&] { return release_first_call; });
            }
        }
        return FakeGui::Reply{Json{{"jsonrpc", "2.0"},
                                   {"id", request.at("id")},
                                   {"result",
                                    {{"is_error", false},
                                     {"structured", {{"completed", true}}},
                                     {"text", "{\"completed\":true}"}}}}};
    });

    std::string input;
    appendBridgeLine(input, modernBridgeRequest(1, "server/discover"));
    appendBridgeLine(input, modernBridgeRequest(2, "tools/call", bridgeCallParams("cancel")));
    appendBridgeLine(
        input, OrderedJson{{"jsonrpc", "2.0"},
                           {"method", "notifications/cancelled"},
                           {"params", modernParams(OrderedJson{{"requestId", 2},
                                                               {"reason", "no longer needed"}})}});
    appendBridgeLine(input, modernBridgeRequest(4, "tools/list"));
    appendBridgeLine(input, modernBridgeRequest(3, "tools/call", bridgeCallParams("continue")));

    BlockingInputBuffer input_buffer;
    input_buffer.append(input);
    input_buffer.closeInput();
    std::istream in(&input_buffer);
    FlushObservingBuffer output_buffer;
    std::ostream out(&output_buffer);
    std::ostringstream err;
    std::atomic<int> bridge_status{-1};
    std::thread bridge_thread(
        [&] { bridge_status = runBridge(in, out, err, bridgeOptions(gui.endpointFile())); });

    bool first_call_waiting = false;
    {
        std::unique_lock lock(gui_mutex);
        first_call_waiting = gui_changed.wait_for(lock, 2s, [&] { return first_call_entered; });
    }
    const bool cancellation_processed_while_call_waited = output_buffer.waitForResponse(4, 2s);
    {
        std::lock_guard lock(gui_mutex);
        release_first_call = true;
    }
    gui_changed.notify_all();
    bridge_thread.join();

    std::vector<std::string> observed_gui_call_order;
    {
        std::lock_guard lock(gui_mutex);
        observed_gui_call_order = gui_call_order;
    }
    const auto stdout_text = output_buffer.snapshot();
    const BridgeRun run{bridge_status.load(), stdout_text, err.str(),
                        parseBridgeOutput(stdout_text)};
    CHECK(first_call_waiting);
    CHECK(cancellation_processed_while_call_waited);
    CHECK(run.status == 0);
    CHECK((observed_gui_call_order == std::vector<std::string>{"cancel", "continue"}));
    CHECK(bridgeResponse(run, 2) == nullptr);
    const auto *later_call = bridgeResponse(run, 3);
    REQUIRE(later_call != nullptr);
    REQUIRE(later_call->contains("result"));
    REQUIRE(later_call->at("result").at("content").size() == 1);
    CHECK(later_call->at("result").at("content")[0].at("text") == "{\"completed\":true}");
}

TEST_CASE("CRLF-terminated input lines parse", "[bridge]") {
    const auto request = modernBridgeRequest(23, "server/discover");
    std::string input;
    appendBridgeLine(input, request, "\r\n");

    const auto run = invokeBridge(std::move(input), bridgeOptions());
    CHECK(run.status == 0);
    const auto *response = bridgeResponse(run, 23);
    REQUIRE(response != nullptr);
    REQUIRE(response->contains("result"));
    CHECK(response->at("result").at("supportedVersions") ==
          Json::array({std::string(kModernVersion)}));
}

TEST_CASE("An oversized stdin line gets an error with a null id", "[bridge]") {
    std::string input(kAgentMaxLineBytes + 1, 'x');
    input += '\n';
    appendBridgeLine(input, modernBridgeRequest(24, "server/discover"));

    const auto start = std::chrono::steady_clock::now();
    const auto run = invokeBridge(std::move(input), bridgeOptions());
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(elapsed < 10s);
    CHECK(run.status == 0);
    REQUIRE(run.messages.size() == 2);
    CHECK(run.messages[0].at("id").is_null());
    REQUIRE(run.messages[0].contains("error"));
    CHECK(run.messages[0].at("error").at("code") == -32600);
    const auto *after_oversized_line = bridgeResponse(run, 24);
    REQUIRE(after_oversized_line != nullptr);
    CHECK(after_oversized_line->at("result").at("supportedVersions") ==
          Json::array({std::string(kModernVersion)}));
}

TEST_CASE("Client identity control characters are escaped in stderr", "[bridge]") {
    const std::string client_name = "client\nInjected\t\x01";
    const std::string client_version = "1.0\r";
    std::string input;
    appendBridgeLine(
        input, rpcRequest(1, "initialize",
                          OrderedJson{{"protocolVersion", "2025-11-25"},
                                      {"capabilities", OrderedJson::object()},
                                      {"clientInfo",
                                       {{"name", client_name}, {"version", client_version}}}}));

    const auto run = invokeBridge(std::move(input), bridgeOptions());
    CHECK(run.status == 0);
    CHECK(
        run.stderr_text.find("rf-sim-mcp: client\\nInjected\\t\\x01 1.0\\r, protocol 2025-11-25") !=
        std::string::npos);
    const auto newline = run.stderr_text.find('\n');
    REQUIRE(newline != std::string::npos);
    CHECK(run.stderr_text.find('\n', newline + 1) == std::string::npos);
}

TEST_CASE("Unexpected bridge failures use a generic INTERNAL result", "[bridge]") {
    const auto result = mcp_bridge_detail::internalFailureResult();

    CHECK(result.is_error);
    CHECK(result.structured.at("epoch").is_null());
    CHECK(result.structured.at("error").at("code") == "INTERNAL");
    CHECK(result.structured.at("error").at("message") == "Internal bridge error.");
    CHECK(result.structured.dump().find("exception") == std::string::npos);
}

TEST_CASE("A throwing GUI call becomes the generic INTERNAL result and the bridge keeps serving",
          "[bridge]") {
    for (const bool std_exception : {true, false}) {
        CAPTURE(std_exception);
        auto options = bridgeOptions();
        options.call_override = [std_exception](const std::string &,
                                                const OrderedJson &) -> AgentToolResult {
            if (std_exception)
                throw std::runtime_error("secret detail that must not reach the client");
            throw 42;
        };
        std::string input;
        appendBridgeLine(input,
                         modernBridgeRequest(5, "tools/call", bridgeCallParams("library_search")));
        appendBridgeLine(input, modernBridgeRequest(6, "tools/list"));

        const auto run = invokeBridge(std::move(input), options);
        CHECK(run.status == 0);
        const auto *failed = bridgeResponse(run, 5);
        REQUIRE(failed != nullptr);
        CHECK(failed->at("result").at("isError") == true);
        CHECK(failed->at("result").at("structuredContent").at("error").at("code") == "INTERNAL");
        CHECK(failed->dump().find("secret") == std::string::npos);
        CHECK(run.stderr_text.find("bridge call") != std::string::npos);
        CHECK(bridgeResponse(run, 6) != nullptr);
    }
}

TEST_CASE("Stdout carries only JSON-RPC messages", "[bridge]") {
    std::string first_input;
    appendBridgeLine(
        first_input,
        rpcRequest(1, "initialize",
                   OrderedJson{{"protocolVersion", "2025-11-25"},
                               {"capabilities", OrderedJson::object()},
                               {"clientInfo", {{"name", "test-client"}, {"version", "1.0"}}}}));

    BlockingInputBuffer input_buffer;
    input_buffer.append(first_input);
    std::istream in(&input_buffer);
    FlushObservingBuffer output_buffer;
    std::ostream out(&output_buffer);
    std::ostringstream err;
    std::atomic<int> bridge_status{-1};
    std::atomic<bool> bridge_finished{false};
    std::thread bridge_thread([&] {
        bridge_status = runBridge(in, out, err, bridgeOptions());
        bridge_finished = true;
    });

    const bool initialize_flushed_while_input_open = output_buffer.waitForResponse(1, 2s);
    const bool bridge_waited_for_more_input = !bridge_finished.load();
    std::string later_input;
    appendBridgeLine(later_input,
                     OrderedJson{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
    appendBridgeLine(later_input, rpcRequest(2, "tools/list"));
    input_buffer.append(later_input);
    const bool list_flushed_while_input_open = output_buffer.waitForResponse(2, 2s);
    const auto flushes_before_eof = output_buffer.flushCount();
    input_buffer.closeInput();
    bridge_thread.join();

    const auto stdout_text = output_buffer.snapshot();
    const auto messages = parseBridgeOutput(stdout_text);
    CHECK(initialize_flushed_while_input_open);
    CHECK(bridge_waited_for_more_input);
    CHECK(list_flushed_while_input_open);
    CHECK(flushes_before_eof >= 2);
    CHECK(bridge_status.load() == 0);
    REQUIRE(messages.size() == 2);
    for (const auto &message : messages) {
        CHECK(message.at("jsonrpc") == "2.0");
        CHECK(message.contains("id"));
        CHECK((message.contains("result") || message.contains("error")));
    }
    const std::string expected_log = "rf-sim-mcp: test-client 1.0, protocol 2025-11-25";
    const auto first_log = err.str().find(expected_log);
    REQUIRE(first_log != std::string::npos);
    CHECK(err.str().rfind(expected_log) == first_log);
}

TEST_CASE("tools/list works with the GUI closed", "[bridge]") {
    const auto missing_directory = std::filesystem::temp_directory_path() /
                                   ("rfsim-bridge-closed-" + test_temp_paths::processTag());
    std::error_code ignored;
    std::filesystem::remove_all(missing_directory, ignored);

    std::string input;
    appendBridgeLine(
        input,
        rpcRequest(1, "initialize",
                   OrderedJson{{"protocolVersion", "2025-11-25"},
                               {"capabilities", OrderedJson::object()},
                               {"clientInfo", {{"name", "test-client"}, {"version", "1.0"}}}}));
    appendBridgeLine(input,
                     OrderedJson{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
    appendBridgeLine(input, rpcRequest(2, "tools/list"));

    const auto run =
        invokeBridge(std::move(input), bridgeOptions(missing_directory / "agent-endpoint.json"));
    CHECK(run.status == 0);
    const auto *list = bridgeResponse(run, 2);
    REQUIRE(list != nullptr);
    REQUIRE(list->contains("result"));
    CHECK(list->at("result").at("tools").size() == 8);
}

TEST_CASE("A modern tools/call without clientInfo does not reuse the previous GUI identity",
          "[bridge]") {
    FakeGui gui;
    const auto endpoint = gui.endpoint();
    std::mutex hello_mutex;
    std::vector<Json> hello_requests;
    gui.setHelloScript([&](const Json &request) -> std::optional<FakeGui::Reply> {
        {
            std::lock_guard lock(hello_mutex);
            hello_requests.push_back(request);
        }
        return FakeGui::Reply{Json{{"jsonrpc", "2.0"},
                                   {"id", request.at("id")},
                                   {"result",
                                    {{"gui_token", endpoint.gui_token},
                                     {"app_version", "test-gui"},
                                     {"catalog_version", kAgentCatalogVersion},
                                     {"epoch", 1}}}}};
    });

    const OrderedJson named_client{{"name", "identified-client"}, {"version", "4.5.6"}};
    std::string input;
    appendBridgeLine(input, modernBridgeRequest(1, "server/discover"));
    appendBridgeLine(
        input, modernBridgeRequest(2, "tools/call", bridgeCallParams("identified"), named_client));
    appendBridgeLine(input, modernBridgeRequest(3, "tools/call", bridgeCallParams("anonymous")));

    const auto run = invokeBridge(std::move(input), bridgeOptions(gui.endpointFile()));
    CHECK(run.status == 0);
    REQUIRE(gui.helloCount() == 2);
    {
        std::lock_guard lock(hello_mutex);
        REQUIRE(hello_requests.size() == 2);
        const auto &first_client = hello_requests[0].at("params").at("client");
        CHECK(first_client.at("name") == "identified-client");
        CHECK(first_client.at("version") == "4.5.6");
        const auto &later_client = hello_requests[1].at("params").at("client");
        CHECK(later_client.at("name") == "");
        CHECK(later_client.at("version") == "");
    }
    for (const Json id : {Json(2), Json(3)}) {
        const auto *response = bridgeResponse(run, id);
        REQUIRE(response != nullptr);
        REQUIRE(response->contains("result"));
        CHECK(response->at("result").at("isError") == false);
    }
}
} // namespace

TEST_CASE("initialize negotiates the legacy versions", "[mcp]") {
    SECTION("the supported 2025-06-18 version is echoed") {
        McpSession session(testIdentity());
        const auto step = initializeLegacy(session, "2025-06-18");
        REQUIRE(step.replies.size() == 1);
        CHECK(step.replies[0].at("result").at("protocolVersion") == "2025-06-18");
    }

    SECTION("the supported 2025-11-25 version is echoed") {
        McpSession session(testIdentity());
        const auto step = initializeLegacy(session, "2025-11-25");
        REQUIRE(step.replies.size() == 1);
        CHECK(step.replies[0].at("result").at("protocolVersion") == "2025-11-25");
    }

    SECTION("an older version falls back to 2025-11-25") {
        McpSession session(testIdentity());
        const auto step = initializeLegacy(session, "2024-11-05");
        REQUIRE(step.replies.size() == 1);
        CHECK(step.replies[0].at("result").at("protocolVersion") == "2025-11-25");
    }
}

TEST_CASE("Legacy sessions serve ping, tools/list and tools/call", "[mcp]") {
    McpSession session(testIdentity());
    const auto initialized = initializeLegacy(session, "2025-11-25");
    REQUIRE(initialized.replies.size() == 1);
    const auto &initialize_result = initialized.replies[0].at("result");
    const auto &legacy_capabilities = initialize_result.at("capabilities");
    CHECK(legacy_capabilities.size() == 1);
    CHECK(legacy_capabilities.contains("tools"));
    CHECK_FALSE(legacy_capabilities.contains("resources"));
    CHECK_FALSE(legacy_capabilities.contains("prompts"));
    CHECK(initialize_result.at("capabilities").at("tools").at("listChanged") == false);
    CHECK(initialize_result.at("serverInfo").at("name") == "test-rf-mcp");
    CHECK(initialize_result.at("serverInfo").at("title") == "Test RF Simulator");
    CHECK(initialize_result.at("serverInfo").at("version") == "3.2.1");
    CHECK(initialize_result.at("instructions").is_string());

    const auto legacy_client = session.clientInfo();
    CHECK(legacy_client.first == "test-client");
    CHECK(legacy_client.second == "1.0");

    const auto ping = sendLegacy(session, 2, "ping");
    REQUIRE(ping.replies.size() == 1);
    CHECK(ping.replies[0].at("result") == Json::object());
    CHECK_FALSE(ping.replies[0].at("result").contains("resultType"));

    const auto list = sendLegacy(session, 3, "tools/list");
    REQUIRE(list.replies.size() == 1);
    const auto &list_result = list.replies[0].at("result");
    CHECK_FALSE(list_result.contains("resultType"));
    const auto &tools = list_result.at("tools");
    constexpr std::array<std::string_view, 8> expected_tools{
        "component_types",        "library_search", "circuit_get",
        "component_get",          "circuit_edit",   "measure_port",
        "network_analyzer_sweep", "data_file_read"};
    REQUIRE(tools.size() == expected_tools.size());
    const auto *catalog_tool = findAgentTool("component_types");
    REQUIRE(catalog_tool != nullptr);
    CHECK(tools[0].at("inputSchema") == catalog_tool->input_schema);
    CHECK(tools[0].at("outputSchema") == catalog_tool->output_schema);
    CHECK(tools[0].at("annotations") == catalog_tool->annotations);
    for (std::size_t i = 0; i < expected_tools.size(); ++i) {
        CHECK(tools[i].at("name") == expected_tools[i]);
    }

    OrderedJson call_params{{"name", "library_search"},
                            {"arguments", OrderedJson{{"query", "LNA"}}}};
    const auto call_step = sendLegacy(session, 4, "tools/call", std::move(call_params));
    REQUIRE(call_step.call.has_value());
    CHECK(call_step.call->id == 4);
    CHECK(call_step.call->tool == "library_search");
    CHECK(call_step.call->modern == false);

    const AgentToolResult tool_result{false, Json{{"parts", Json::array()}, {"total", 0}}};
    const auto response = session.toolCallResponse(*call_step.call, tool_result);
    REQUIRE(response.contains("result"));
    const auto &call_result = response.at("result");
    CHECK_FALSE(call_result.contains("resultType"));
    REQUIRE(call_result.at("content").size() == 1);
    CHECK(call_result.at("content")[0].at("type") == "text");
    CHECK(call_result.at("content")[0].at("text") == tool_result.structured.dump());

    const AgentToolResult error_result{true, Json{{"error", "validation failed"}}};
    const auto error_response = session.toolCallResponse(*call_step.call, error_result);
    REQUIRE(error_response.at("result").at("content").size() == 1);
    CHECK(error_response.at("result").at("isError") == true);
    CHECK(error_response.at("result").at("content")[0].at("text") ==
          error_result.structured.dump());
}

TEST_CASE("A legacy initialize does not lock modern per-request protocol selection", "[mcp]") {
    McpSession session(testIdentity());
    const auto initialized = initializeLegacy(session, "2025-11-25");
    REQUIRE(initialized.replies.size() == 1);

    const auto modern = sendModern(session, 2, "tools/list");
    REQUIRE(modern.replies.size() == 1);
    REQUIRE(modern.replies[0].contains("result"));
    checkModernServerInfo(modern.replies[0].at("result"));
    CHECK(modern.replies[0].at("result").at("tools").size() == 8);
}

TEST_CASE("A modern request does not replace the identity for later legacy calls", "[mcp]") {
    McpSession session(testIdentity());
    REQUIRE(initializeLegacy(session, "2025-11-25").replies.size() == 1);

    const OrderedJson modern_client_info{{"name", "modern-client"}, {"version", "7.8.9"}};
    const auto modern = sendModern(session, 2, "tools/list", OrderedJson::object(),
                                   std::string(kModernVersion), true, modern_client_info);
    REQUIRE(modern.replies.size() == 1);
    CHECK(session.clientInfo().first == "modern-client");

    const auto legacy_call =
        sendLegacy(session, 3, "tools/call",
                   OrderedJson{{"name", "library_search"}, {"arguments", OrderedJson::object()}});
    REQUIRE(legacy_call.call.has_value());
    CHECK_FALSE(legacy_call.call->modern);
    const auto legacy_client_info = session.clientInfo();
    CHECK(legacy_client_info.first == "test-client");
    CHECK(legacy_client_info.second == "1.0");
}

TEST_CASE("server/discover advertises only the modern version, with caching hints", "[mcp]") {
    McpSession session(testIdentity());
    const auto step = sendModern(session, 1, "server/discover");
    REQUIRE(step.replies.size() == 1);
    const auto &result = step.replies[0].at("result");
    checkModernServerInfo(result);
    const auto &supported = result.at("supportedVersions");
    REQUIRE(supported.size() == 1);
    CHECK(supported[0] == kModernVersion);
    CHECK(result.at("ttlMs") == 3600000);
    CHECK(result.at("cacheScope") == "private");
    const auto &capabilities = result.at("capabilities");
    CHECK(capabilities.size() == 1);
    CHECK(capabilities.contains("tools"));
    CHECK_FALSE(capabilities.contains("resources"));
    CHECK_FALSE(capabilities.contains("prompts"));
    CHECK(result.at("capabilities").at("tools").is_object());
    REQUIRE(result.at("instructions").is_string());
    CHECK_FALSE(result.at("instructions").get<std::string>().empty());
}

TEST_CASE("Modern results carry resultType, serverInfo and caching hints", "[mcp]") {
    McpSession session(testIdentity());

    const auto list = sendModern(session, 10, "tools/list");
    REQUIRE(list.replies.size() == 1);
    const auto &list_result = list.replies[0].at("result");
    checkModernServerInfo(list_result);
    CHECK(list_result.at("ttlMs") == 3600000);
    CHECK(list_result.at("cacheScope") == "private");
    REQUIRE(list_result.at("tools").size() == 8);
    constexpr std::array<std::string_view, 8> expected_tools{
        "component_types",        "library_search", "circuit_get",
        "component_get",          "circuit_edit",   "measure_port",
        "network_analyzer_sweep", "data_file_read"};
    const auto &tools = list_result.at("tools");
    for (std::size_t i = 0; i < expected_tools.size(); ++i) {
        CHECK(tools[i].at("name") == expected_tools[i]);
    }
    const auto *catalog_tool = findAgentTool("component_types");
    REQUIRE(catalog_tool != nullptr);
    CHECK(list_result.at("tools")[0].at("inputSchema") == catalog_tool->input_schema);
    CHECK(list_result.at("tools")[0].at("outputSchema") == catalog_tool->output_schema);
    CHECK(list_result.at("tools")[0].at("annotations") == catalog_tool->annotations);

    OrderedJson call_params{{"name", "library_search"},
                            {"arguments", OrderedJson{{"query", "LNA"}}}};
    const auto call_step = sendModern(session, 11, "tools/call", std::move(call_params));
    REQUIRE(call_step.call.has_value());
    CHECK(call_step.call->modern);
    const AgentToolResult tool_result{false, Json{{"parts", Json::array()}, {"total", 0}}};
    const auto response = session.toolCallResponse(*call_step.call, tool_result);
    REQUIRE(response.contains("result"));
    const auto &call_result = response.at("result");
    checkModernServerInfo(call_result);
    REQUIRE(call_result.at("content").size() == 1);
    CHECK(call_result.at("content")[0].at("text") == tool_result.structured.dump());
}

TEST_CASE("An unsupported modern version is UnsupportedProtocolVersionError", "[mcp]") {
    McpSession session(testIdentity());
    const auto step = sendModern(session, 1, "tools/list", OrderedJson::object(), "2099-01-01");
    REQUIRE(step.replies.size() == 1);
    const auto &error = rpcError(step);
    CHECK(error.at("code") == -32022);
    CHECK(error.at("message") == "Unsupported protocol version");
    REQUIRE(error.at("data").at("supported").size() == 1);
    CHECK(error.at("data").at("supported")[0] == kModernVersion);
    CHECK(error.at("data").at("requested") == "2099-01-01");
}

TEST_CASE("A modern request without clientCapabilities is invalid params", "[mcp]") {
    McpSession session(testIdentity());
    const auto step = sendModern(session, 1, "tools/list", OrderedJson::object(),
                                 std::string(kModernVersion), false);
    REQUIRE(step.replies.size() == 1);
    CHECK(rpcError(step).at("code") == -32602);
}

TEST_CASE("Modern requests accept omitted optional clientInfo without reusing stale identity",
          "[mcp]") {
    McpSession session(testIdentity());
    const OrderedJson first_client_info{{"name", "identified-client"}, {"version", "4.5.6"}};
    const auto identified = sendModern(session, 1, "tools/list", OrderedJson::object(),
                                       std::string(kModernVersion), true, first_client_info);
    REQUIRE(identified.replies.size() == 1);
    REQUIRE(identified.replies[0].contains("result"));
    auto client = session.clientInfo();
    CHECK(client.first == "identified-client");
    CHECK(client.second == "4.5.6");

    const auto anonymous = sendModern(session, 2, "server/discover");
    REQUIRE(anonymous.replies.size() == 1);
    REQUIRE(anonymous.replies[0].contains("result"));
    checkModernServerInfo(anonymous.replies[0].at("result"));
    client = session.clientInfo();
    CHECK(client.first.empty());
    CHECK(client.second.empty());
}

TEST_CASE("ping is not a modern method", "[mcp]") {
    McpSession session(testIdentity());
    const auto step = sendModern(session, 1, "ping");
    REQUIRE(step.replies.size() == 1);
    CHECK(rpcError(step).at("code") == -32601);
}

TEST_CASE("Unknown tools and malformed tools/call are -32602", "[mcp]") {
    McpSession session(testIdentity());
    REQUIRE(initializeLegacy(session, "2025-11-25").replies.size() == 1);

    SECTION("an unknown tool names the requested tool") {
        const auto step =
            sendLegacy(session, 1, "tools/call",
                       OrderedJson{{"name", "nope"}, {"arguments", OrderedJson::object()}});
        REQUIRE(step.replies.size() == 1);
        CHECK(rpcError(step).at("code") == -32602);
        CHECK(rpcError(step).at("message") == "Unknown tool: nope");
    }

    SECTION("a tools/call without a name is invalid params") {
        const auto step =
            sendLegacy(session, 2, "tools/call", OrderedJson{{"arguments", OrderedJson::object()}});
        REQUIRE(step.replies.size() == 1);
        CHECK(rpcError(step).at("code") == -32602);
    }

    SECTION("non-object tool arguments are invalid params") {
        const auto step = sendLegacy(session, 3, "tools/call",
                                     OrderedJson{{"name", "library_search"}, {"arguments", 5}});
        REQUIRE(step.replies.size() == 1);
        CHECK(rpcError(step).at("code") == -32602);
    }

    SECTION("non-object method params are invalid params") {
        const auto step = sendLegacy(session, 4, "tools/list", OrderedJson::array());
        REQUIRE(step.replies.size() == 1);
        CHECK(rpcError(step).at("code") == -32602);
    }

    SECTION("non-object _meta is invalid params and cannot dispatch a tool call") {
        const OrderedJson params{{"name", "library_search"},
                                 {"arguments", OrderedJson::object()},
                                 {"_meta", OrderedJson::array()}};
        const auto step = sendLegacy(session, 5, "tools/call", params);
        REQUIRE(step.replies.size() == 1);
        CHECK(rpcError(step).at("code") == -32602);
        CHECK_FALSE(step.call.has_value());
    }
}

TEST_CASE("MCP tool calls preserve argument member order", "[mcp]") {
    McpSession session(testIdentity());
    REQUIRE(initializeLegacy(session, "2025-11-25").replies.size() == 1);

    OrderedJson arguments = OrderedJson::object();
    arguments["type"] = "amplifier";
    arguments["query"] = "LNA";
    arguments["limit"] = 10;
    const auto step =
        sendLegacy(session, 7, "tools/call",
                   OrderedJson{{"name", "library_search"}, {"arguments", std::move(arguments)}});
    REQUIRE(step.call.has_value());

    std::vector<std::string> observed_keys;
    for (auto it = step.call->arguments.begin(); it != step.call->arguments.end(); ++it) {
        observed_keys.push_back(it.key());
    }
    REQUIRE(observed_keys.size() == 3);
    CHECK(observed_keys[0] == "type");
    CHECK(observed_keys[1] == "query");
    CHECK(observed_keys[2] == "limit");
}

TEST_CASE("Envelope errors use the JSON-RPC codes", "[mcp]") {
    McpSession session(testIdentity());
    REQUIRE(initializeLegacy(session, "2025-11-25").replies.size() == 1);

    SECTION("malformed JSON is a parse error with a null id") {
        const auto step = session.onLine("{");
        REQUIRE(step.replies.size() == 1);
        CHECK(step.replies[0].at("id").is_null());
        CHECK(rpcError(step).at("code") == -32700);
    }

    SECTION("batch messages are rejected as invalid requests") {
        const OrderedJson batch = OrderedJson::array({rpcRequest(1, "ping")});
        const auto step = session.onLine(batch.dump());
        REQUIRE(step.replies.size() == 1);
        CHECK(step.replies[0].at("id").is_null());
        CHECK(rpcError(step).at("code") == -32600);
    }

    SECTION("unknown methods are method-not-found errors") {
        const auto step = sendLegacy(session, 9, "not/a/method");
        REQUIRE(step.replies.size() == 1);
        CHECK(step.replies[0].at("id") == 9);
        CHECK(rpcError(step).at("code") == -32601);
    }
    SECTION("a non-string JSON-RPC version is an invalid request") {
        const OrderedJson request{{"jsonrpc", 2}, {"id", 10}, {"method", "tools/list"}};
        const auto step = session.onLine(request.dump());
        REQUIRE(step.replies.size() == 1);
        CHECK(rpcError(step).at("code") == -32600);
    }

    SECTION("notifications receive no reply") {
        const OrderedJson notification{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}};
        const auto step = session.onLine(notification.dump());
        CHECK(step.replies.empty());
    }

    SECTION("an oversized line is an invalid request with a null id") {
        const auto step = session.onOversizedLine();
        REQUIRE(step.replies.size() == 1);
        CHECK(step.replies[0].at("id").is_null());
        CHECK(rpcError(step).at("code") == -32600);
    }
}

TEST_CASE("MCP request IDs must be strings or integers", "[mcp]") {
    SECTION("a structured ID produces an invalid request with a null response ID") {
        McpSession session(testIdentity());
        const OrderedJson request{
            {"jsonrpc", "2.0"}, {"id", OrderedJson{{"invalid", true}}}, {"method", "tools/list"}};
        const auto step = session.onLine(request.dump());
        REQUIRE(step.replies.size() == 1);
        CHECK(step.replies[0].at("id").is_null());
        CHECK(rpcError(step).at("code") == -32600);
    }

    SECTION("modern requests reject null IDs") {
        McpSession session(testIdentity());
        const auto step = sendModern(session, OrderedJson(nullptr), "tools/list");
        REQUIRE(step.replies.size() == 1);
        CHECK(step.replies[0].at("id").is_null());
        CHECK(rpcError(step).at("code") == -32600);
    }

    SECTION("modern requests reject fractional numeric IDs") {
        McpSession session(testIdentity());
        const auto step = sendModern(session, OrderedJson(1.5), "tools/list");
        REQUIRE(step.replies.size() == 1);
        CHECK(step.replies[0].at("id").is_null());
        CHECK(rpcError(step).at("code") == -32600);
    }
}

TEST_CASE("Notifications never receive replies or dispatch request-only methods", "[mcp]") {
    SECTION("an initialize notification does not activate the legacy session") {
        McpSession session(testIdentity());
        OrderedJson params{
            {"protocolVersion", "2025-11-25"},
            {"capabilities", OrderedJson::object()},
            {"clientInfo", OrderedJson{{"name", "test-client"}, {"version", "1.0"}}}};
        const OrderedJson notification{
            {"jsonrpc", "2.0"}, {"method", "initialize"}, {"params", std::move(params)}};
        const auto step = session.onLine(notification.dump());
        CHECK(step.replies.empty());

        const auto pre_initialize = sendLegacy(session, 2, "tools/list");
        REQUIRE(pre_initialize.replies.size() == 1);
        CHECK(rpcError(pre_initialize).at("code") == -32600);
    }

    SECTION("modern discovery notifications do not receive results") {
        McpSession session(testIdentity());
        const OrderedJson notification{
            {"jsonrpc", "2.0"}, {"method", "server/discover"}, {"params", modernParams()}};
        const auto step = session.onLine(notification.dump());
        CHECK(step.replies.empty());
    }

    SECTION("tool-call notifications are not dispatched") {
        McpSession session(testIdentity());
        REQUIRE(initializeLegacy(session, "2025-11-25").replies.size() == 1);
        const OrderedJson call_params = modernParams(
            OrderedJson{{"name", "library_search"}, {"arguments", OrderedJson::object()}});
        const OrderedJson notification{
            {"jsonrpc", "2.0"}, {"method", "tools/call"}, {"params", call_params}};
        const auto step = session.onLine(notification.dump());
        CHECK(step.replies.empty());
        CHECK_FALSE(step.call.has_value());
    }
}

TEST_CASE("Notification-only methods reject requests with IDs", "[mcp]") {
    SECTION("notifications/initialized with an ID is method not found") {
        McpSession session(testIdentity());
        REQUIRE(initializeLegacy(session, "2025-11-25").replies.size() == 1);
        const auto step = sendLegacy(session, 3, "notifications/initialized");
        REQUIRE(step.replies.size() == 1);
        CHECK(step.replies[0].at("id") == 3);
        CHECK(rpcError(step).at("code") == -32601);
    }

    SECTION("notifications/cancelled with an ID does not cancel") {
        McpSession session(testIdentity());
        REQUIRE(initializeLegacy(session, "2025-11-25").replies.size() == 1);
        const auto step =
            sendLegacy(session, 4, "notifications/cancelled", OrderedJson{{"requestId", 42}});
        REQUIRE(step.replies.size() == 1);
        CHECK(step.replies[0].at("id") == 4);
        CHECK(rpcError(step).at("code") == -32601);
        CHECK_FALSE(step.cancel_id.has_value());
    }

    SECTION("modern cancellation with an ID is not a notification") {
        McpSession session(testIdentity());
        const auto step =
            sendModern(session, 5, "notifications/cancelled", OrderedJson{{"requestId", 42}});
        REQUIRE(step.replies.size() == 1);
        CHECK(step.replies[0].at("id") == 5);
        CHECK(rpcError(step).at("code") == -32601);
        CHECK_FALSE(step.cancel_id.has_value());
    }
}

TEST_CASE("Malformed client identity fields are invalid params", "[mcp]") {
    SECTION("legacy initialize rejects malformed identity without activating the session") {
        McpSession session(testIdentity());
        OrderedJson params{{"protocolVersion", "2025-11-25"},
                           {"capabilities", OrderedJson::object()},
                           {"clientInfo", OrderedJson{{"name", 42}, {"version", "1.0"}}}};
        const auto step = session.onLine(rpcRequest(1, "initialize", std::move(params)).dump());
        REQUIRE(step.replies.size() == 1);
        CHECK(rpcError(step).at("code") == -32602);

        const auto pre_initialize = sendLegacy(session, 2, "tools/list");
        REQUIRE(pre_initialize.replies.size() == 1);
        CHECK(rpcError(pre_initialize).at("code") == -32600);
    }

    SECTION("a modern request rejects malformed optional identity") {
        McpSession session(testIdentity());
        const OrderedJson client_info{{"name", 42}, {"version", "1.0"}};
        const auto step = sendModern(session, 1, "tools/list", OrderedJson::object(),
                                     std::string(kModernVersion), true, client_info);
        REQUIRE(step.replies.size() == 1);
        CHECK(rpcError(step).at("code") == -32602);
        CHECK(session.clientInfo().first.empty());
        CHECK(session.clientInfo().second.empty());
    }
}

TEST_CASE("Cancellation names the request to drop", "[mcp]") {
    SECTION("modern cancellation returns its request ID") {
        McpSession session(testIdentity());
        OrderedJson params{{"requestId", 37}, {"reason", "no longer needed"}};
        params = modernParams(std::move(params));
        const OrderedJson notification{{"jsonrpc", "2.0"},
                                       {"method", "notifications/cancelled"},
                                       {"params", std::move(params)}};
        const auto step = session.onLine(notification.dump());
        CHECK(step.replies.empty());
        REQUIRE(step.cancel_id.has_value());
        CHECK(*step.cancel_id == 37);
    }

    SECTION("legacy cancellation returns its request ID") {
        McpSession session(testIdentity());
        REQUIRE(initializeLegacy(session, "2025-11-25").replies.size() == 1);
        const OrderedJson notification{{"jsonrpc", "2.0"},
                                       {"method", "notifications/cancelled"},
                                       {"params", OrderedJson{{"requestId", "legacy-request-8"},
                                                              {"reason", "no longer needed"}}}};
        const auto step = session.onLine(notification.dump());
        CHECK(step.replies.empty());
        REQUIRE(step.cancel_id.has_value());
        CHECK(*step.cancel_id == "legacy-request-8");
    }
}

TEST_CASE("Requests before initialize without modern _meta are rejected", "[mcp]") {
    McpSession session(testIdentity());
    const auto step = sendLegacy(session, 1, "tools/list");
    REQUIRE(step.replies.size() == 1);
    const auto &error = rpcError(step);
    CHECK(error.at("code") == -32600);
    CHECK(error.at("message") ==
          "send initialize first, or use protocol version 2026-07-28 with per-request _meta");
}

TEST_CASE("Calls relay to the GUI and back", "[gui_link]") {
    FakeGui gui;
    GuiLink link(gui.endpointFile(), {});
    link.setClientInfo("test-client", "4.2");
    const auto result = link.call("library_search", OrderedJson{{"query", "LNA"}});
    CHECK_FALSE(result.is_error);
    const Json expected_success{{"ok", true}};
    CHECK(result.structured == expected_success);
    CHECK(gui.authenticated());
    CHECK(gui.helloRequest().at("params").at("bridge_token") == gui.endpoint().bridge_token);
    CHECK(gui.helloRequest().at("params").at("catalog_version") == kAgentCatalogVersion);
    CHECK(gui.helloRequest().at("params").at("client").at("name") == "test-client");
    CHECK(gui.helloRequest().at("params").at("client").at("version") == "4.2");
    CHECK(gui.lastCall().at("params").at("tool") == "library_search");
    const Json expected_arguments{{"query", "LNA"}};
    CHECK(gui.lastCall().at("params").at("arguments") == expected_arguments);
}

TEST_CASE("A missing endpoint is SIMULATOR_UNAVAILABLE and the next call retries", "[gui_link]") {
    const auto missing_directory = std::filesystem::temp_directory_path() /
                                   ("rfsim-missing-gui-" + test_temp_paths::processTag());
    struct Cleanup {
        std::filesystem::path directory;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    } cleanup{missing_directory};
    std::error_code ignored;
    std::filesystem::remove_all(missing_directory, ignored);
    const auto missing = missing_directory / "agent-endpoint.json";
    GuiLink link(missing, {});
    const auto unavailable = link.call("library_search", OrderedJson::object());

    CHECK(unavailable.is_error);
    CHECK(unavailable.structured.at("error").at("code") == "SIMULATOR_UNAVAILABLE");
    CHECK(unavailable.structured.at("error").at("message") ==
          "RF Simulator is not reachable. Open RF Simulator, turn on the Agent Server in View > "
          "Agent, then retry this call.");

    FakeGui gui;
    std::string error;
    REQUIRE(ensurePrivateDirectory(missing_directory, &error));
    REQUIRE(writeAgentEndpoint(missing, gui.endpoint(), &error));
    const auto retried = link.call("library_search", OrderedJson::object());
    CHECK_FALSE(retried.is_error);
}

TEST_CASE("A hello reply with the wrong gui_token is dropped", "[gui_link]") {
    FakeGui gui(std::string(64, 'd'));
    const auto endpoint = readAgentEndpoint(gui.endpointFile(), nullptr);
    REQUIRE(endpoint.has_value());
    // A hostile endpoint claims a different token than the real server returns.
    auto altered = *endpoint;
    altered.gui_token = std::string(64, 'e');
    REQUIRE(writeAgentEndpoint(gui.endpointFile(), altered, nullptr));
    GuiLink link(gui.endpointFile(), {});
    const auto result = link.call("library_search", OrderedJson::object());
    CHECK(result.is_error);
    CHECK(result.structured.at("error").at("code") == "SIMULATOR_UNAVAILABLE");
    CHECK(gui.lastCall().empty());
}

TEST_CASE("A catalog mismatch is VERSION_MISMATCH", "[gui_link]") {
    const int local_catalog = kAgentCatalogVersion;
    const int remote_catalog = local_catalog + 1;
    FakeGui gui(std::string(64, 'c'), remote_catalog);
    auto endpoint = readAgentEndpoint(gui.endpointFile(), nullptr);
    REQUIRE(endpoint.has_value());
    endpoint->catalog_version = local_catalog;
    std::string error;
    REQUIRE(writeAgentEndpoint(gui.endpointFile(), *endpoint, &error));
    CHECK(endpoint->catalog_version == local_catalog);
    GuiLink link(gui.endpointFile(), {});
    const auto result = link.call("library_search", OrderedJson::object());
    CHECK(result.is_error);
    CHECK(result.structured.at("error").at("code") == "VERSION_MISMATCH");
    CHECK(result.structured.at("error").at("message") ==
          "rf-sim-mcp catalog " + std::to_string(local_catalog) + ", RF Simulator catalog " +
              std::to_string(remote_catalog) + "; install the same release of both");
    CHECK(gui.helloCount() == 1);
}

TEST_CASE("Silent or dying GUIs fail fast", "[gui_link]") {
    SECTION("accept-only listener exceeds the bounded hello wait") {
        FakeGui gui;
        gui.holdHelloReply();
        GuiLink link(gui.endpointFile(), {.connect = 100ms, .hello = 100ms, .call = 10s});
        const auto start = std::chrono::steady_clock::now();
        const auto result = link.call("library_search", OrderedJson::object());
        CHECK(result.is_error);
        CHECK(result.structured.at("error").at("code") == "SIMULATOR_UNAVAILABLE");
        REQUIRE(gui.helloCount() == 1);
        CHECK(std::chrono::steady_clock::now() - start < 1100ms);
    }
    SECTION("GUI closes after receiving the call without replying") {
        FakeGui gui;
        gui.closeAfterCall();
        GuiLink link(gui.endpointFile(), {.connect = 100ms, .hello = 500ms, .call = 10s});
        const auto start = std::chrono::steady_clock::now();
        const auto result = link.call("library_search", OrderedJson::object());
        CHECK(result.is_error);
        CHECK(result.structured.at("error").at("code") == "SIMULATOR_UNAVAILABLE");
        CHECK(result.structured.at("error").at("message") ==
              "RF Simulator is not reachable. Open RF Simulator, turn on the Agent Server in View "
              "> Agent, then retry this call.");
        CHECK(std::chrono::steady_clock::now() - start < 1s);
        REQUIRE(gui.lastCall().at("method") == "call");
    }
}

TEST_CASE("A call with no reply times out and drops the connection", "[gui_link]") {
    FakeGui gui;
    gui.holdReplies();
    GuiLink link(gui.endpointFile(), {.connect = 100ms, .hello = 500ms, .call = 200ms});
    const auto start = std::chrono::steady_clock::now();
    const auto result = link.call("library_search", OrderedJson::object());
    CHECK(result.is_error);
    CHECK(result.structured.at("error").at("code") == "SIMULATOR_UNAVAILABLE");
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(elapsed >= 180ms);
    CHECK(elapsed < 1s);
    REQUIRE(gui.lastCall().at("method") == "call");
    REQUIRE(gui.helloCount() == 1);
    gui.holdReplies(false);
    const auto retried = link.call("library_search", OrderedJson::object());
    CHECK_FALSE(retried.is_error);
    CHECK(gui.helloCount() == 2);
}

TEST_CASE("GUI JSON-RPC errors map data.code to tool errors", "[gui_link]") {
    FakeGui gui;
    gui.setScript([](const Json &request) -> std::optional<FakeGui::Reply> {
        return FakeGui::Reply{Json{{"jsonrpc", "2.0"},
                                   {"id", request.at("id")},
                                   {"error",
                                    {{"code", kAgentRpcErrorCode},
                                     {"message", "bad arguments"},
                                     {"data", {{"code", "INVALID_ARGUMENT"}}}}}}};
    });
    GuiLink link(gui.endpointFile(), {});
    const auto result = link.call("library_search", OrderedJson::object());
    CHECK(result.is_error);
    CHECK(result.structured.at("error").at("code") == "INVALID_ARGUMENT");
    CHECK(result.structured.contains("epoch"));
    CHECK(result.structured.value("epoch", Json::object()).is_null());
    CHECK(result.structured.at("error").at("message") == "bad arguments");
}
TEST_CASE("Malformed GUI JSON-RPC replies are unavailable", "[gui_link]") {
    SECTION("non-object error during hello") {
        FakeGui gui;
        gui.setHelloScript([](const Json &request) -> std::optional<FakeGui::Reply> {
            return FakeGui::Reply{
                Json{{"jsonrpc", "2.0"}, {"id", request.at("id")}, {"error", true}}};
        });
        GuiLink link(gui.endpointFile(), {});
        const auto result = link.call("library_search", OrderedJson::object());
        CHECK(result.is_error);
        CHECK(result.structured.at("error").at("code") == "SIMULATOR_UNAVAILABLE");
    }

    SECTION("non-string jsonrpc version during tools/call") {
        FakeGui gui;
        gui.setScript([](const Json &request) -> std::optional<FakeGui::Reply> {
            return FakeGui::Reply{
                Json{{"jsonrpc", 2},
                     {"id", request.at("id")},
                     {"result", {{"is_error", false}, {"structured", Json::object()}}}}};
        });
        GuiLink link(gui.endpointFile(), {});
        const auto result = link.call("library_search", OrderedJson::object());
        CHECK(result.is_error);
        CHECK(result.structured.at("error").at("code") == "SIMULATOR_UNAVAILABLE");
    }

    SECTION("non-object error during tools/call") {
        FakeGui gui;
        gui.setScript([](const Json &request) -> std::optional<FakeGui::Reply> {
            return FakeGui::Reply{
                Json{{"jsonrpc", "2.0"}, {"id", request.at("id")}, {"error", true}}};
        });
        GuiLink link(gui.endpointFile(), {});
        const auto result = link.call("library_search", OrderedJson::object());
        CHECK(result.is_error);
        CHECK(result.structured.at("error").at("code") == "SIMULATOR_UNAVAILABLE");
    }
    SECTION("non-string error code during hello") {
        FakeGui gui;
        gui.setHelloScript([](const Json &request) -> std::optional<FakeGui::Reply> {
            return FakeGui::Reply{Json{{"jsonrpc", "2.0"},
                                       {"id", request.at("id")},
                                       {"error",
                                        {{"code", kAgentRpcErrorCode},
                                         {"message", "malformed mismatch"},
                                         {"data", {{"code", 7}}}}}}};
        });
        GuiLink link(gui.endpointFile(), {});
        const auto result = link.call("library_search", OrderedJson::object());
        CHECK(result.is_error);
        CHECK(result.structured.at("error").at("code") == "SIMULATOR_UNAVAILABLE");
    }
    SECTION("non-integer JSON-RPC error code during tools/call") {
        FakeGui gui;
        gui.setScript([](const Json &request) -> std::optional<FakeGui::Reply> {
            return FakeGui::Reply{Json{{"jsonrpc", "2.0"},
                                       {"id", request.at("id")},
                                       {"error",
                                        {{"code", true},
                                         {"message", "bad arguments"},
                                         {"data", {{"code", "INVALID_ARGUMENT"}}}}}}};
        });
        GuiLink link(gui.endpointFile(), {});
        const auto result = link.call("library_search", OrderedJson::object());
        CHECK(result.is_error);
        CHECK(result.structured.at("error").at("code") == "SIMULATOR_UNAVAILABLE");
    }

    SECTION("non-string JSON-RPC error message during tools/call") {
        FakeGui gui;
        gui.setScript([](const Json &request) -> std::optional<FakeGui::Reply> {
            return FakeGui::Reply{Json{{"jsonrpc", "2.0"},
                                       {"id", request.at("id")},
                                       {"error",
                                        {{"code", kAgentRpcErrorCode},
                                         {"message", 7},
                                         {"data", {{"code", "INVALID_ARGUMENT"}}}}}}};
        });
        GuiLink link(gui.endpointFile(), {});
        const auto result = link.call("library_search", OrderedJson::object());
        CHECK(result.is_error);
        CHECK(result.structured.at("error").at("code") == "SIMULATOR_UNAVAILABLE");
    }

    SECTION("non-string data code during tools/call") {
        FakeGui gui;
        gui.setScript([](const Json &request) -> std::optional<FakeGui::Reply> {
            return FakeGui::Reply{Json{{"jsonrpc", "2.0"},
                                       {"id", request.at("id")},
                                       {"error",
                                        {{"code", kAgentRpcErrorCode},
                                         {"message", "bad arguments"},
                                         {"data", {{"code", 7}}}}}}};
        });
        GuiLink link(gui.endpointFile(), {});
        const auto result = link.call("library_search", OrderedJson::object());
        CHECK(result.is_error);
        CHECK(result.structured.at("error").at("code") == "SIMULATOR_UNAVAILABLE");
    }
}

namespace {
// Raw text, so building and dumping the request never recurses over the nesting.
std::string deepArgumentsRequest(std::size_t depth) {
    return R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"library_search","arguments":{"deep":)" +
           std::string(depth, '[') + std::string(depth, ']') + "}}}";
}
} // namespace

TEST_CASE("Tool arguments nested beyond the depth limit never reach a recursive copy", "[mcp]") {
    McpSession session(testIdentity());
    REQUIRE(initializeLegacy(session, "2025-11-25").replies.size() == 1);

    SECTION("arguments within the limit are forwarded") {
        const auto step = session.onLine(deepArgumentsRequest(40));
        CHECK(step.call.has_value());
    }
    SECTION("arguments past the limit are an invalid request") {
        const auto step = session.onLine(deepArgumentsRequest(100));
        CHECK_FALSE(step.call.has_value());
        REQUIRE(step.replies.size() == 1);
        CHECK(rpcError(step).at("code") == -32600);
    }
    SECTION("arguments far past the limit cannot overflow the bridge") {
        const auto step = session.onLine(deepArgumentsRequest(200000));
        CHECK_FALSE(step.call.has_value());
        REQUIRE(step.replies.size() == 1);
        CHECK(rpcError(step).at("code") == -32600);
    }
}

TEST_CASE("GuiLink refuses over-deep arguments before contacting the GUI", "[gui_link]") {
    GuiLink link(std::filesystem::temp_directory_path() /
                     ("rfsim-deep-arguments-" + test_temp_paths::processTag()) /
                     "agent-endpoint.json",
                 {});
    OrderedJson deep = OrderedJson::array();
    for (std::size_t level = 1; level < 100; ++level) {
        OrderedJson outer = OrderedJson::array();
        outer.push_back(std::move(deep));
        deep = std::move(outer);
    }
    const auto result = link.call("library_search", OrderedJson{{"deep", std::move(deep)}});
    CHECK(result.is_error);
    CHECK(result.structured.at("error").at("code") == "INVALID_ARGUMENT");
}

TEST_CASE("A notification nested beyond the depth limit gets no response", "[mcp]") {
    McpSession session(testIdentity());
    REQUIRE(initializeLegacy(session, "2025-11-25").replies.size() == 1);
    // An id-less object is a notification, so its depth refusal must not answer it.
    const std::string deep =
        R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":)" +
        std::string(100, '[') + std::string(100, ']') + "}}";
    const auto step = session.onLine(deep);
    CHECK(step.replies.empty());
    CHECK_FALSE(step.cancel_id.has_value());
}

TEST_CASE("A malformed id-less object beyond the depth limit gets an invalid request", "[mcp]") {
    McpSession session(testIdentity());
    REQUIRE(initializeLegacy(session, "2025-11-25").replies.size() == 1);
    const std::string nesting = std::string(100, '[') + std::string(100, ']') + "}}";
    SECTION("a wrong jsonrpc version is answered with a null id") {
        const auto step = session.onLine(
            R"({"jsonrpc":"1.0","method":"notifications/cancelled","params":{"requestId":)" +
            nesting);
        REQUIRE(step.replies.size() == 1);
        CHECK(step.replies[0].at("id").is_null());
        CHECK(rpcError(step).at("code") == -32600);
    }
    SECTION("a missing method is answered with a null id") {
        const auto step = session.onLine(R"({"jsonrpc":"2.0","params":{"requestId":)" + nesting);
        REQUIRE(step.replies.size() == 1);
        CHECK(step.replies[0].at("id").is_null());
        CHECK(rpcError(step).at("code") == -32600);
    }
}

TEST_CASE("initialize rejects a non-object _meta before activating the session", "[mcp]") {
    McpSession session(testIdentity());
    const OrderedJson params{{"protocolVersion", "2025-11-25"},
                             {"capabilities", OrderedJson::object()},
                             {"clientInfo", {{"name", "test-client"}, {"version", "1.0"}}},
                             {"_meta", 5}};
    const auto initialized = sendLegacy(session, 1, "initialize", params);
    REQUIRE(initialized.replies.size() == 1);
    REQUIRE(initialized.replies[0].contains("error"));
    CHECK(rpcError(initialized).at("code") == -32602);
    const auto list = sendLegacy(session, 2, "tools/list");
    REQUIRE(list.replies.size() == 1);
    CHECK(rpcError(list).at("code") == -32600);
}

TEST_CASE("An unreachable GUI reports the OS connect cause, not a timeout", "[gui_link]") {
    std::string error;
    auto listener = AgentListener::bindLoopback(&error);
    REQUIRE(listener.has_value());
    const int closed_port = listener->port();
    listener->close();

    const auto dir = std::filesystem::temp_directory_path() /
                     ("rfsim-refused-gui-" + test_temp_paths::processTag());
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{dir};
    REQUIRE(ensurePrivateDirectory(dir, &error));
    AgentEndpoint endpoint;
    endpoint.port = closed_port;
    endpoint.bridge_token = std::string(64, 'b');
    endpoint.gui_token = std::string(64, 'd');
    endpoint.app_version = "test";
    endpoint.catalog_version = kAgentCatalogVersion;
    endpoint.pid = 1;
    const auto file = dir / "agent-endpoint.json";
    REQUIRE(writeAgentEndpoint(file, endpoint, &error));

    GuiLink link(file, {});
    const auto result = link.call("library_search", OrderedJson::object());
    CHECK(result.is_error);
    CHECK(result.structured.at("error").at("code") == "SIMULATOR_UNAVAILABLE");
    CHECK(link.lastConnectError().find("connect loopback") != std::string::npos);
    CHECK(link.lastConnectError().find("timed out") == std::string::npos);

    std::string input;
    appendBridgeLine(input,
                     modernBridgeRequest(5, "tools/call", bridgeCallParams("library_search")));
    auto options = bridgeOptions(file);
    options.timeouts = GuiLinkTimeouts{}; // the production default, not the suite's 200 ms
    const auto run = invokeBridge(std::move(input), options);
    CHECK(run.stderr_text.find("RF Simulator unreachable: connect loopback") != std::string::npos);
    CHECK(run.stderr_text.find("timed out") == std::string::npos);
}
