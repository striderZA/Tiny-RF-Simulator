#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

inline constexpr int kAgentCatalogVersion = 1;

struct AgentToolDefinition {
    std::string name, title, description;
    nlohmann::json input_schema, output_schema, annotations;
};

const std::vector<AgentToolDefinition> &agentToolCatalog();
const AgentToolDefinition *findAgentTool(std::string_view name);
nlohmann::json agentErrorSchema();
const std::string &agentServerInstructions();
