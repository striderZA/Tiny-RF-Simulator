#pragma once

#include "agent_errors.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

struct McpServerIdentity {
    std::string name = "rf-sim-mcp";
    std::string title = "RF Simulator";
    std::string version;
};

struct McpToolCall {
    nlohmann::json id;
    std::string tool;
    nlohmann::ordered_json arguments;
    bool modern = false;
};

struct McpStep {
    std::vector<nlohmann::json> replies;
    std::optional<McpToolCall> call;
    std::optional<nlohmann::json> cancel_id;
};

class McpSession {
  public:
    explicit McpSession(McpServerIdentity identity);

    McpStep onLine(const std::string &line);
    McpStep onOversizedLine();
    nlohmann::json toolCallResponse(const McpToolCall &call, const AgentToolResult &result) const;
    // Identity for the latest request: per-request in the modern era, initialized identity in the
    // legacy era.
    std::pair<std::string, std::string> clientInfo() const;

  private:
    McpServerIdentity m_identity;
    bool m_initialized = false;
    std::string m_legacy_client_name;
    std::string m_legacy_client_version;
    std::string m_current_client_name;
    std::string m_current_client_version;
};
