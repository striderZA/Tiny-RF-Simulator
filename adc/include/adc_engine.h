#pragma once
#include "node_graph_engine.h"
#include "signal_node.h"
class AdcEngine {
public:
    AdcEngine(int id, NodeGraphEngine& graph);
    void update(double dt);
    SignalNode& node();
private:
    int m_id;
};
