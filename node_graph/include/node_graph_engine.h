#pragma once

#include "signal_node.h"
#include <string>
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

    SignalNode *getSourceForInput(int input_pin_id) const;
    std::vector<SignalNode *> getSourcesForInput(int input_pin_id) const;

    int activeProbePin() const { return m_active_probe_pin; }
    void setActiveProbePin(int pin_id) { m_active_probe_pin = pin_id; }
    SignalNode *probedSignalNode() const;

    const std::vector<GraphNode> &nodes() const { return m_nodes; }
    const std::vector<GraphLink> &links() const { return m_links; }

  private:
    int m_next_node_id = 1;
    int m_next_pin_id = 100;
    int m_next_link_id = 1000;
    int m_active_probe_pin = -1;
    std::vector<GraphNode> m_nodes;
    std::vector<GraphLink> m_links;
};
