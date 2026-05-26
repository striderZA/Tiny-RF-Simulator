#pragma once

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
    void rebuildPinMap();
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

  private:
    int m_next_node_id = 1;
    int m_next_pin_id = 100;
    int m_next_link_id = 1000;
    std::vector<int> m_probe_pins;
    std::vector<GraphNode> m_nodes;
    std::vector<GraphLink> m_links;
    mutable std::unordered_map<int, SignalNode*> m_pin_to_source;
};
