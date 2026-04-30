#pragma once

#include "common.h"
#include "node_graph_engine.h"
#include "signal_node.h"

class SplitterEngine {
  public:
    SplitterEngine(int id, NodeGraphEngine& graph);
    int id() const { return m_id; }
    int graphNodeId() const { return m_graph_node_id; }
    int inputPinId() const;
    int outputPinId(int index) const;

    void update(double dt);

    SignalNode& node() { return m_node; }

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;

    SignalNode m_node;

    static constexpr double SPLIT_LOSS_DB = 3.010299956639812;
};
