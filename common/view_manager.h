#pragma once

#include "logging_core.h"
#include "signal_node.h"
#include <vector>

class ViewManager {
  public:
    void registerNode(SignalNode *node) {
        if (node) {
            LOG_INFO("Register new node: %d inputs | %d outputs.", node->input.tones.size(),
                     node->output.tones.size());
            m_nodes.push_back(node);
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

    const std::vector<SignalNode *> &nodes() const { return m_nodes; }

  private:
    std::vector<SignalNode *> m_nodes;
};
