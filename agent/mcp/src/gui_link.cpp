#include "gui_link.h"

#include "agent_catalog.h"
#include "agent_endpoint.h"
#include "agent_socket.h"
#include "agent_token.h"
#include "agent_wire.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace {
using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;
using Clock = std::chrono::steady_clock;

constexpr const char *kUnavailableMessage =
    "RF Simulator is not reachable. Open RF Simulator, turn on the Agent Server in View > Agent, "
    "then retry this call.";

std::chrono::milliseconds remaining(Clock::time_point deadline) {
    const auto now = Clock::now();
    if (now >= deadline)
        return std::chrono::milliseconds::zero();
    return std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
}

AgentToolResult unavailable() {
    return agentErrorResult({AgentErrorCode::SimulatorUnavailable, kUnavailableMessage,
                             std::nullopt, std::nullopt, Json::object()},
                            std::nullopt);
}

AgentToolResult invalidArguments() {
    return agentErrorResult({AgentErrorCode::InvalidArgument,
                             "tool arguments are nested too deeply", std::nullopt, std::nullopt,
                             Json::object()},
                            std::nullopt);
}

AgentToolResult versionMismatch(int bridge, int gui) {
    return agentErrorResult({AgentErrorCode::VersionMismatch,
                             "rf-sim-mcp catalog " + std::to_string(bridge) +
                                 ", RF Simulator catalog " + std::to_string(gui) +
                                 "; install the same release of both",
                             std::nullopt, std::nullopt, Json::object()},
                            std::nullopt);
}

bool validRpcReply(const Json &reply, const Json &id) {
    return reply.is_object() && reply.contains("jsonrpc") && reply.at("jsonrpc").is_string() &&
           reply.at("jsonrpc") == "2.0" && reply.contains("id") && reply.at("id") == id;
}

bool validRpcError(const Json &error) {
    return error.is_object() && error.contains("code") && error.at("code").is_number_integer() &&
           error.contains("message") && error.at("message").is_string() && error.contains("data") &&
           error.at("data").is_object() && error.at("data").contains("code") &&
           error.at("data").at("code").is_string();
}

} // namespace

class GuiLink::Impl {
  public:
    Impl(std::filesystem::path file, GuiLinkTimeouts limits)
        : endpoint_file(std::move(file)), timeouts(limits) {}

    void drop() {
        if (channel)
            channel->close();
        channel.reset();
    }

    enum class ConnectResult { Ready, Unavailable, VersionMismatch };

    ConnectResult connect() {
        if (channel && channel->isOpen())
            return ConnectResult::Ready;
        drop();

        std::string error;
        const auto endpoint = readAgentEndpoint(endpoint_file, &error);
        if (!endpoint)
            return ConnectResult::Unavailable;
        if (endpoint->catalog_version != kAgentCatalogVersion) {
            mismatch_version = endpoint->catalog_version;
            return ConnectResult::VersionMismatch;
        }

        auto connected = connectAgentLoopback(endpoint->port, timeouts.connect, &error);
        if (!connected) {
            connect_error = error;
            return ConnectResult::Unavailable;
        }
        channel.emplace(std::move(*connected));

        const auto deadline = Clock::now() + timeouts.hello;
        const Json id = next_id++;
        const OrderedJson hello{
            {"jsonrpc", "2.0"},
            {"id", id},
            {"method", "hello"},
            {"params",
             {{"protocol", kAgentWireProtocol},
              {"bridge_token", endpoint->bridge_token},
              {"catalog_version", kAgentCatalogVersion},
              {"client", {{"name", client_name}, {"version", client_version}}}}}};
        if (!channel->writeLine(hello.dump(), remaining(deadline))) {
            drop();
            return ConnectResult::Unavailable;
        }

        std::string line;
        if (channel->readLine(line, remaining(deadline)) != AgentReadStatus::Line) {
            drop();
            return ConnectResult::Unavailable;
        }
        Json reply;
        try {
            reply = Json::parse(line);
        } catch (...) {
            drop();
            return ConnectResult::Unavailable;
        }
        if (!validRpcReply(reply, id)) {
            drop();
            return ConnectResult::Unavailable;
        }
        if (reply.contains("error")) {
            const auto &rpc_error = reply.at("error");
            if (!validRpcError(rpc_error)) {
                drop();
                return ConnectResult::Unavailable;
            }
            const auto &data = rpc_error.at("data");
            drop();
            if (data.at("code") == "VERSION_MISMATCH") {
                mismatch_version = endpoint->catalog_version;
                return ConnectResult::VersionMismatch;
            }
            return ConnectResult::Unavailable;
        }
        try {
            const auto &result = reply.at("result");
            if (!agentTokensEqual(result.at("gui_token").get<std::string>(), endpoint->gui_token)) {
                drop();
                return ConnectResult::Unavailable;
            }
            if (!result.at("catalog_version").is_number_integer()) {
                drop();
                return ConnectResult::Unavailable;
            }
            const int gui_catalog = result.at("catalog_version").get<int>();
            if (gui_catalog != kAgentCatalogVersion) {
                drop();
                mismatch_version = gui_catalog;
                return ConnectResult::VersionMismatch;
            }
        } catch (...) {
            drop();
            return ConnectResult::Unavailable;
        }
        return ConnectResult::Ready;
    }

    AgentToolResult call(const std::string &tool, const OrderedJson &arguments) {
        connect_error.clear();
        if (!agentJsonDepthWithin(arguments))
            return invalidArguments();
        const auto connection = connect();
        if (connection == ConnectResult::Unavailable)
            return unavailable();
        if (connection == ConnectResult::VersionMismatch)
            return versionMismatch(kAgentCatalogVersion, mismatch_version);

        const auto deadline = Clock::now() + timeouts.call;
        const Json id = next_id++;
        const OrderedJson request{{"jsonrpc", "2.0"},
                                  {"id", id},
                                  {"method", "call"},
                                  {"params", {{"tool", tool}, {"arguments", arguments}}}};
        if (!channel->writeLine(request.dump(), remaining(deadline))) {
            drop();
            return unavailable();
        }
        std::string line;
        if (channel->readLine(line, remaining(deadline)) != AgentReadStatus::Line) {
            drop();
            return unavailable();
        }
        Json reply;
        try {
            reply = Json::parse(line);
        } catch (...) {
            drop();
            return unavailable();
        }
        if (!validRpcReply(reply, id)) {
            drop();
            return unavailable();
        }
        if (reply.contains("error")) {
            const auto &rpc_error = reply.at("error");
            if (!validRpcError(rpc_error)) {
                drop();
                return unavailable();
            }
            drop();
            const auto &data = rpc_error.at("data");
            const std::string code = data.at("code").get<std::string>();
            const std::string message = rpc_error.at("message").get<std::string>();
            Json details = data;
            details.erase("code");
            details.erase("message");
            return {true,
                    {{"epoch", nullptr},
                     {"error", {{"code", code}, {"message", message}, {"details", details}}}}};
        }
        try {
            const auto &result = reply.at("result");
            return {result.at("is_error").get<bool>(), result.at("structured")};
        } catch (...) {
            drop();
            return unavailable();
        }
    }

    std::filesystem::path endpoint_file;
    GuiLinkTimeouts timeouts;
    std::optional<AgentChannel> channel;
    std::uint64_t next_id{1};
    std::string client_name, client_version;
    int mismatch_version{kAgentCatalogVersion};
    std::string connect_error;
};

GuiLink::GuiLink(std::filesystem::path endpoint_file, GuiLinkTimeouts timeouts)
    : m_impl(std::make_unique<Impl>(std::move(endpoint_file), timeouts)) {}
GuiLink::~GuiLink() = default;
GuiLink::GuiLink(GuiLink &&) noexcept = default;
GuiLink &GuiLink::operator=(GuiLink &&) noexcept = default;

void GuiLink::setClientInfo(std::string name, std::string version) {
    m_impl->client_name = std::move(name);
    m_impl->client_version = std::move(version);
}

AgentToolResult GuiLink::call(const std::string &tool, const nlohmann::ordered_json &arguments) {
    return m_impl->call(tool, arguments);
}

std::string GuiLink::lastConnectError() const { return m_impl->connect_error; }
