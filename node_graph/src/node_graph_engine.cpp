#include "node_graph_engine.h"
#include "logging_core.h"
#include <algorithm>

int NodeGraphEngine::addNode(const std::string &label, SignalNode *signal_node, int num_inputs,
                             int num_outputs) {
    GraphNode node;
    node.node_id = m_next_node_id++;
    node.input_pin_ids.reserve(num_inputs);
    for (int i = 0; i < num_inputs; ++i) {
        node.input_pin_ids.push_back(m_next_pin_id++);
    }
    node.output_pin_ids.reserve(num_outputs);
    for (int i = 0; i < num_outputs; ++i) {
        node.output_pin_ids.push_back(m_next_pin_id++);
    }
    node.signal_node = signal_node;
    node.label = label;
    m_nodes.push_back(node);
    LOG_INFO("Added node '%s' (id=%d)", label.c_str(), node.node_id);
    return node.node_id;
}

void NodeGraphEngine::removeNode(int node_id) {
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(),
                           [node_id](const GraphNode &n) { return n.node_id == node_id; });
    if (it == m_nodes.end())
        return;

    LOG_INFO("Removed node '%s' (id=%d)", it->label.c_str(), node_id);

    // Collect all pin IDs belonging to this node
    std::vector<int> node_pins;
    node_pins.reserve(it->input_pin_ids.size() + it->output_pin_ids.size());
    node_pins.insert(node_pins.end(), it->input_pin_ids.begin(), it->input_pin_ids.end());
    node_pins.insert(node_pins.end(), it->output_pin_ids.begin(), it->output_pin_ids.end());

    // Remove all links connected to any of this node's pins
    m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                                  [&node_pins](const GraphLink &l) {
                                      return std::find(node_pins.begin(), node_pins.end(),
                                                       l.start_pin_id) != node_pins.end() ||
                                             std::find(node_pins.begin(), node_pins.end(),
                                                       l.end_pin_id) != node_pins.end();
                                  }),
                  m_links.end());

    if (!it->output_pin_ids.empty() &&
        std::find(it->output_pin_ids.begin(), it->output_pin_ids.end(), m_active_probe_pin) !=
            it->output_pin_ids.end()) {
        m_active_probe_pin = -1;
    }

    m_nodes.erase(it);
}

int NodeGraphEngine::addLink(int start_pin, int end_pin) {
    GraphLink link;
    link.link_id = m_next_link_id++;
    link.start_pin_id = start_pin;
    link.end_pin_id = end_pin;
    m_links.push_back(link);
    LOG_INFO("Connected pins: %d -> %d (link id=%d)", start_pin, end_pin, link.link_id);
    return link.link_id;
}

void NodeGraphEngine::removeLink(int link_id) {
    auto it = std::find_if(m_links.begin(), m_links.end(),
                            [link_id](const GraphLink &l) { return l.link_id == link_id; });
    if (it != m_links.end()) {
        LOG_INFO("Disconnected pins: %d -> %d (link id=%d)", it->start_pin_id, it->end_pin_id, link_id);
        m_links.erase(it);
    }
}

SignalNode *NodeGraphEngine::getSourceForInput(int input_pin_id) const {
    if (input_pin_id < 0)
        return nullptr;
    for (const auto &link : m_links) {
        if (link.end_pin_id == input_pin_id) {
            for (const auto &node : m_nodes) {
                for (int pin : node.output_pin_ids) {
                    if (pin == link.start_pin_id) {
                        return node.signal_node;
                    }
                }
            }
        }
    }
    return nullptr;
}

std::vector<SignalNode *> NodeGraphEngine::getSourcesForInput(int input_pin_id) const {
    std::vector<SignalNode *> result;
    if (input_pin_id < 0)
        return result;
    for (const auto &link : m_links) {
        if (link.end_pin_id == input_pin_id) {
            for (const auto &node : m_nodes) {
                for (int pin : node.output_pin_ids) {
                    if (pin == link.start_pin_id) {
                        result.push_back(node.signal_node);
                        break;
                    }
                }
            }
        }
    }
    return result;
}

SignalNode *NodeGraphEngine::probedSignalNode() const {
    if (m_active_probe_pin < 0)
        return nullptr;
    for (const auto &node : m_nodes) {
        for (int pin : node.output_pin_ids) {
            if (pin == m_active_probe_pin) {
                return node.signal_node;
            }
        }
    }
    return nullptr;
}
