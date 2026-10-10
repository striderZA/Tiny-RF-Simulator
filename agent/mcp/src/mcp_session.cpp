#include "mcp_session.h"

#include "agent_catalog.h"
#include "agent_wire.h"

#include <string_view>

namespace {
using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;
constexpr std::string_view kModernVersion = "2026-07-28";
constexpr std::string_view kProtocolMeta = "io.modelcontextprotocol/protocolVersion";
constexpr std::string_view kCapabilitiesMeta = "io.modelcontextprotocol/clientCapabilities";
constexpr std::string_view kClientInfoMeta = "io.modelcontextprotocol/clientInfo";

Json serverInfo(const McpServerIdentity &identity) {
    return Json{{"name", identity.name}, {"title", identity.title}, {"version", identity.version}};
}
Json rpcError(const Json &id, int code, std::string message, Json data = nullptr) {
    Json error{{"code", code}, {"message", std::move(message)}};
    if (!data.is_null())
        error["data"] = std::move(data);
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"error", std::move(error)}};
}
Json rpcResult(const Json &id, Json result) {
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}
Json modernResult(Json result, const McpServerIdentity &identity) {
    result["resultType"] = "complete";
    result["_meta"] = Json{{"io.modelcontextprotocol/serverInfo", serverInfo(identity)}};
    return result;
}

bool validClientInfo(const OrderedJson &client_info) {
    return client_info.is_object() && client_info.contains("name") &&
           client_info["name"].is_string() && client_info.contains("version") &&
           client_info["version"].is_string();
}
bool validId(const Json &id) { return id.is_string() || id.is_number_integer(); }
} // namespace

McpSession::McpSession(McpServerIdentity identity) : m_identity(std::move(identity)) {}

McpStep McpSession::onOversizedLine() {
    McpStep step;
    step.replies.push_back(rpcError(nullptr, -32600, "Invalid Request"));
    return step;
}

McpStep McpSession::onLine(const std::string &line) {
    McpStep step;
    OrderedJson request;
    try {
        request = OrderedJson::parse(line);
    } catch (...) {
        step.replies.push_back(rpcError(nullptr, -32700, "Parse error"));
        return step;
    }
    if (!agentJsonDepthWithin(request)) {
        // Copy only a scalar id: a nested one must not reach the recursive id copy below.
        const bool scalar_id = request.is_object() && request.contains("id") &&
                               !request["id"].is_array() && !request["id"].is_object();
        step.replies.push_back(
            rpcError(scalar_id ? Json(request["id"]) : Json(nullptr), -32600, "Invalid Request"));
        return step;
    }
    if (!request.is_object()) {
        step.replies.push_back(rpcError(nullptr, -32600, "Invalid Request"));
        return step;
    }
    const Json id = request.contains("id") ? Json(request["id"]) : Json(nullptr);
    const bool notification = !request.contains("id");
    auto fail = [&](int code, std::string message, Json data = nullptr) {
        if (!notification)
            step.replies.push_back(rpcError(id, code, std::move(message), std::move(data)));
    };
    const bool invalid_id = !notification && !validId(id);
    if (!request.contains("jsonrpc") || !request["jsonrpc"].is_string() ||
        request["jsonrpc"] != "2.0" || !request.contains("method") ||
        !request["method"].is_string() || invalid_id) {
        if (invalid_id)
            step.replies.push_back(rpcError(nullptr, -32600, "Invalid Request"));
        else if (notification)
            step.replies.push_back(rpcError(nullptr, -32600, "Invalid Request"));
        else
            fail(-32600, "Invalid Request");
        return step;
    }
    const std::string method = request["method"].get<std::string>();
    if (method == "initialize") {
        if (notification)
            return step;
        if (!request.contains("params") || !request["params"].is_object()) {
            fail(-32602, "Invalid params");
            return step;
        }
        const auto &params = request["params"];
        if (!params.contains("protocolVersion") || !params["protocolVersion"].is_string() ||
            !params.contains("capabilities") || !params["capabilities"].is_object() ||
            !params.contains("clientInfo") || !validClientInfo(params["clientInfo"])) {
            fail(-32602, "Invalid params");
            return step;
        }
        const std::string &version = params["protocolVersion"].get_ref<const std::string &>();
        const std::string selected =
            version == "2025-06-18" || version == "2025-11-25" ? version : "2025-11-25";
        const auto &client_info = params["clientInfo"];
        m_initialized = true;
        m_legacy_client_name = client_info["name"].get<std::string>();
        m_legacy_client_version = client_info["version"].get<std::string>();
        m_current_client_name = m_legacy_client_name;
        m_current_client_version = m_legacy_client_version;
        Json result{{"protocolVersion", selected},
                    {"capabilities", Json{{"tools", Json{{"listChanged", false}}}}},
                    {"serverInfo", serverInfo(m_identity)},
                    {"instructions", agentServerInstructions()}};
        step.replies.push_back(rpcResult(id, std::move(result)));
        return step;
    }
    const OrderedJson empty_params = OrderedJson::object();
    const auto &params = request.contains("params") ? request["params"] : empty_params;
    const OrderedJson empty_meta = OrderedJson::object();
    const auto &meta = params.is_object() && params.contains("_meta") && params["_meta"].is_object()
                           ? params["_meta"]
                           : empty_meta;
    const bool modern = meta.contains(kProtocolMeta);
    if (modern) {
        m_current_client_name.clear();
        m_current_client_version.clear();
        const Json requested = meta[kProtocolMeta];
        if (!requested.is_string() || requested != kModernVersion) {
            fail(-32022, "Unsupported protocol version",
                 Json{{"supported", Json::array({kModernVersion})}, {"requested", requested}});
            return step;
        }
        if (!meta.contains(kCapabilitiesMeta) || !meta[kCapabilitiesMeta].is_object()) {
            fail(-32602, "Invalid params");
            return step;
        }
        if (meta.contains(kClientInfoMeta)) {
            const auto &client_info = meta[kClientInfoMeta];
            if (!validClientInfo(client_info)) {
                fail(-32602, "Invalid params");
                return step;
            }
            m_current_client_name = client_info["name"].get<std::string>();
            m_current_client_version = client_info["version"].get<std::string>();
        }
    } else if (!m_initialized) {
        fail(-32600,
             "send initialize first, or use protocol version 2026-07-28 with per-request _meta");
        return step;
    }
    if (!params.is_object()) {
        fail(-32602, "Invalid params");
        return step;
    }
    if (params.contains("_meta") && !params["_meta"].is_object()) {
        fail(-32602, "Invalid params");
        return step;
    }
    if (!modern) {
        m_current_client_name = m_legacy_client_name;
        m_current_client_version = m_legacy_client_version;
    }
    if (method == "notifications/initialized") {
        if (notification)
            return step;
        fail(-32601, "Method not found");
        return step;
    }
    if (method == "notifications/cancelled") {
        if (!notification) {
            fail(-32601, "Method not found");
            return step;
        }
        if (params.contains("requestId"))
            step.cancel_id = Json(params["requestId"]);
        return step;
    }
    if (notification)
        return step;
    if (method == "ping") {
        if (modern)
            fail(-32601, "Method not found");
        else if (!notification)
            step.replies.push_back(rpcResult(id, Json::object()));
        return step;
    }
    if (method == "server/discover") {
        if (!modern) {
            fail(-32601, "Method not found");
            return step;
        }
        Json result{{"supportedVersions", Json::array({kModernVersion})},
                    {"capabilities", Json{{"tools", Json::object()}}},
                    {"serverInfo", serverInfo(m_identity)},
                    {"instructions", agentServerInstructions()},
                    {"ttlMs", 3600000},
                    {"cacheScope", "private"}};
        step.replies.push_back(rpcResult(id, modernResult(std::move(result), m_identity)));
        return step;
    }
    if (method == "tools/list") {
        Json tools = Json::array();
        for (const auto &tool : agentToolCatalog()) {
            Json entry{{"name", tool.name},
                       {"title", tool.title},
                       {"description", tool.description},
                       {"inputSchema", tool.input_schema},
                       {"outputSchema", tool.output_schema},
                       {"annotations", tool.annotations}};
            tools.push_back(std::move(entry));
        }
        Json result{{"tools", std::move(tools)}};
        if (modern) {
            result["ttlMs"] = 3600000;
            result["cacheScope"] = "private";
            result = modernResult(std::move(result), m_identity);
        }
        if (!notification)
            step.replies.push_back(rpcResult(id, std::move(result)));
        return step;
    }
    if (method == "tools/call") {
        if (!params.contains("name") || !params["name"].is_string()) {
            fail(-32602, "Invalid params");
            return step;
        }
        const std::string tool = params["name"].get<std::string>();
        if (!findAgentTool(tool)) {
            fail(-32602, "Unknown tool: " + tool);
            return step;
        }
        OrderedJson arguments =
            params.contains("arguments") ? params["arguments"] : OrderedJson::object();
        if (!arguments.is_object()) {
            fail(-32602, "Invalid params");
            return step;
        }
        step.call = McpToolCall{id, tool, std::move(arguments), modern};
        return step;
    }
    fail(-32601, "Method not found");
    return step;
}

nlohmann::json McpSession::toolCallResponse(const McpToolCall &call,
                                            const AgentToolResult &result) const {
    Json content = Json::array({Json{{"type", "text"}, {"text", result.structured.dump()}}});
    Json body{{"content", std::move(content)},
              {"isError", result.is_error},
              {"structuredContent", result.structured}};
    if (call.modern)
        body = modernResult(std::move(body), m_identity);
    return rpcResult(call.id, std::move(body));
}
std::pair<std::string, std::string> McpSession::clientInfo() const {
    return {m_current_client_name, m_current_client_version};
}
