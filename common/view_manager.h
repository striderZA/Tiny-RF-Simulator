#pragma once

#include "signal_node.h"
#include <vector>

class ViewManager {
  public:
    void registerNode(SignalNode *node) {
        if (node) {
            m_nodes.push_back(node);
        }
    }

    SignalNode *getActiveNode() const {
        for (auto *n : m_nodes) {
            if (n && n->view_enabled) {
                return n;
            }
            return nullptr;
        }
    }

    const std::vector<SignalNode *> &nodes() const { return m_nodes; }

  private:
    std::vector<SignalNode *> m_nodes;
};
