#pragma once

#include "group.h"
#include "signal_node.h"
#include <string>
#include <unordered_map>
#include <vector>

struct GraphNode {
    int node_id;
    std::vector<int> input_pin_ids;
    std::vector<int> output_pin_ids;
    SignalNode *signal_node;
    std::string label;

    // Per-pin labels for rendering (empty → default "IN"/"OUT")
    std::vector<std::string> input_labels;
    std::vector<std::string> output_labels;
};

struct GraphLink {
    int link_id;
    int start_pin_id;
    int end_pin_id;
};

class NodeGraphEngine {
  public:
    int addNode(const std::string &label, SignalNode *signal_node, int num_inputs, int num_outputs);
    void removeNode(int node_id);

    int addLink(int start_pin, int end_pin);
    void removeLink(int link_id);

    int inputPinId(int node_id) const;
    int outputPinId(int node_id) const;

    SignalNode *getSourceForInput(int input_pin_id) const;
    std::vector<SignalNode *> getSourcesForInput(int input_pin_id) const;
    std::vector<int> topologicalOrder() const;

    static constexpr int MAX_PROBES = 4;

    const std::vector<int>& probePins() const { return m_probe_pins; }
    bool addProbePin(int pin_id);
    bool removeProbePin(int pin_id);
    void clearProbes();
    int probeSlotForPin(int pin_id) const;
    std::vector<SignalNode*> probedSignalNodes() const;

    const std::vector<GraphNode> &nodes() const { return m_nodes; }
    const std::vector<GraphLink> &links() const { return m_links; }

    int nodeIdForPin(int pin_id) const;

    // Set pin labels for a node (for rendering in the node graph widget)
    void setNodePinLabels(int node_id,
                          const std::vector<std::string>& input_labels,
                          const std::vector<std::string>& output_labels);

    // Group operations
    int addGroup(std::string name, std::vector<int> member_node_ids);
    void removeGroup(int group_id);

    // Group collection + accessors
    const std::vector<Group>& groups() const { return m_groups; }
    const Group* groupById(int group_id) const;
    int numGroups() const { return static_cast<int>(m_groups.size()); }
    int selectedGroupId() const { return m_selected_group_id; }
    void setSelectedGroupId(int id);
    void renameGroup(int group_id, std::string name);
    void setGroupCollapsed(int group_id, bool collapsed);
    bool isGroupCollapsed(int group_id) const;
    void rebuildGroupBoundaryPins(int group_id);
    int groupIdForNode(int node_id) const;
    const std::vector<int>& groupsContainingNode(int node_id) const;

  private:
    int m_next_node_id = 1;
    int m_next_pin_id = 100;
    int m_next_link_id = 1000;
    int m_next_group_id = 50000;
    int m_next_boundary_pin_id = 100000;
    int m_selected_group_id = -1;
    std::vector<int> m_probe_pins;
    std::vector<GraphNode> m_nodes;
    std::vector<GraphLink> m_links;
    std::vector<Group> m_groups;
    std::unordered_map<int, std::vector<int>> m_node_to_group_cache;

    void rebuildNodeToGroupCache();
};
