#if __has_include("circuit_runtime.h") || __has_include("editor_commands.h") ||                    \
                                                        __has_include("imgui.h")
#error "mcp_adapter must link no simulator code"
#endif

#include "agent_catalog.h"
#include "agent_errors.h"
#include "mcp_session.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    constexpr std::array<std::string_view, 7> expected_tools{
        "component_types", "library_search", "circuit_get",           "component_get",
        "circuit_edit",    "measure_port",   "network_analyzer_sweep"};
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
    CHECK(modern.replies[0].at("result").at("tools").size() == 7);
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
    REQUIRE(list_result.at("tools").size() == 7);
    constexpr std::array<std::string_view, 7> expected_tools{
        "component_types", "library_search", "circuit_get",           "component_get",
        "circuit_edit",    "measure_port",   "network_analyzer_sweep"};
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
