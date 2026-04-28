#include "node_graph_engine.h"
#include <algorithm>

int NodeGraphEngine::addNode(const std::string& label, SignalNode* signal_node,
                              bool has_input, bool has_output) {
    GraphNode node;
    node.node_id = m_next_node_id++;
    node.input_pin_id = has_input ? m_next_pin_id++ : -1;
    node.output_pin_id = has_output ? m_next_pin_id++ : -1;
    node.signal_node = signal_node;
    node.label = label;
    m_nodes.push_back(node);
    return node.node_id;
}

void NodeGraphEngine::removeNode(int node_id) {
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(),
                           [node_id](const GraphNode& n) { return n.node_id == node_id; });
    if (it == m_nodes.end()) return;

    // Remove all links connected to this node's pins
    auto pin_ids = {it->input_pin_id, it->output_pin_id};
    m_links.erase(
        std::remove_if(m_links.begin(), m_links.end(),
                       [&pin_ids](const GraphLink& l) {
                           return l.start_pin_id == pin_ids.begin()[0] ||
                                  l.start_pin_id == pin_ids.begin()[1] ||
                                  l.end_pin_id == pin_ids.begin()[0] ||
                                  l.end_pin_id == pin_ids.begin()[1];
                       }),
        m_links.end());

    if (m_active_probe_pin == it->output_pin_id) {
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
    return link.link_id;
}

void NodeGraphEngine::removeLink(int link_id) {
    auto it = std::find_if(m_links.begin(), m_links.end(),
                           [link_id](const GraphLink& l) { return l.link_id == link_id; });
    if (it != m_links.end()) {
        m_links.erase(it);
    }
}

SignalNode* NodeGraphEngine::getSourceForInput(int input_pin_id) const {
    if (input_pin_id < 0) return nullptr;
    for (const auto& link : m_links) {
        if (link.end_pin_id == input_pin_id) {
            // Find the node that owns the start_pin
            for (const auto& node : m_nodes) {
                if (node.output_pin_id == link.start_pin_id) {
                    return node.signal_node;
                }
            }
        }
    }
    return nullptr;
}

std::vector<SignalNode*> NodeGraphEngine::getConnectedOutputs(int input_pin_id) const {
    std::vector<SignalNode*> result;
    if (input_pin_id < 0) return result;
    for (const auto& link : m_links) {
        if (link.end_pin_id == input_pin_id) {
            for (const auto& node : m_nodes) {
                if (node.output_pin_id == link.start_pin_id) {
                    result.push_back(node.signal_node);
                    break;
                }
            }
        }
    }
    return result;
}

SignalNode* NodeGraphEngine::probedSignalNode() const {
    if (m_active_probe_pin < 0) return nullptr;
    for (const auto& node : m_nodes) {
        if (node.output_pin_id == m_active_probe_pin) {
            return node.signal_node;
        }
    }
    return nullptr;
}
