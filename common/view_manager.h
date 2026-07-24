#pragma once

#include "logging_core.h"
#include "signal_node.h"
#include <algorithm>
#include <span>
#include <vector>

class ViewManager {
  public:
    void registerNode(SignalNode *node) {
        if (node) {
            LOG_INFO("Register new node: %d inputs | %d outputs.",
                     node->inputs.empty() ? 0
                                          : (node->inputs[0] ? node->inputs[0]->tones.size() : 0),
                     node->outputs.empty() ? 0 : node->outputs[0].tones.size());
            m_nodes.push_back(node);
        }
    }

    void unregisterNode(SignalNode *node) {
        if (!node)
            return;
        auto it = std::find(m_nodes.begin(), m_nodes.end(), node);
        if (it != m_nodes.end()) {
            m_nodes.erase(it);
        }
    }

    SignalNode *getActiveNode() const {
        for (auto *n : m_nodes) {
            if (n && n->view_enabled) {
                return n;
            }
        }
        return nullptr;
    }

    std::vector<SignalNode *> getActiveNodes() const {
        std::vector<SignalNode *> active;
        active.reserve(m_nodes.size());
        for (auto *n : m_nodes) {
            if (n && n->view_enabled) {
                active.push_back(n);
            }
        }
        return active;
    }
    std::span<SignalNode *const> nodes_span() const { return {m_nodes.data(), m_nodes.size()}; }
    const std::vector<SignalNode *> &nodes() const { return m_nodes; }
    void clearNodes() { m_nodes.clear(); }

  private:
    std::vector<SignalNode *> m_nodes;
};
