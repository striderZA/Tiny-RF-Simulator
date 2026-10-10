#include "agent_placement.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

std::vector<AgentPlacement>
planAgentPlacement(const std::vector<int> &added_graph_node_ids,
                   const std::vector<std::optional<std::array<float, 2>>> &positions,
                   const NodeGraphEngine &graph) {
    std::unordered_set<int> added(added_graph_node_ids.begin(), added_graph_node_ids.end());
    std::unordered_map<int, int> columns;

    for (const int node_id : graph.topologicalOrder()) {
        if (!added.contains(node_id))
            continue;

        int preceding_added = 0;
        for (const auto &link : graph.links()) {
            if (graph.nodeIdForPin(link.end_pin_id) != node_id)
                continue;
            const int predecessor = graph.nodeIdForPin(link.start_pin_id);
            const auto found = columns.find(predecessor);
            if (found != columns.end())
                preceding_added = std::max(preceding_added, found->second + 1);
        }
        columns[node_id] = preceding_added;
    }

    std::vector<AgentPlacement> placements;
    placements.reserve(added_graph_node_ids.size());
    std::unordered_map<int, int> rows_by_column;
    for (std::size_t index = 0; index < added_graph_node_ids.size(); ++index) {
        const int node_id = added_graph_node_ids[index];
        const int column = columns.contains(node_id) ? columns.at(node_id) : 0;
        const int row = rows_by_column[column]++;
        const std::optional<std::array<float, 2>> position =
            index < positions.size() ? positions[index] : std::nullopt;
        placements.push_back({node_id, position, column, row});
    }
    return placements;
}
