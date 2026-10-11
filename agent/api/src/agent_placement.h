#pragma once

#include "agent_host.h"
#include "node_graph_engine.h"

#include <array>
#include <optional>
#include <vector>

std::vector<AgentPlacement>
planAgentPlacement(const std::vector<int> &added_graph_node_ids,
                   const std::vector<std::optional<std::array<float, 2>>> &positions,
                   const NodeGraphEngine &graph);
