#pragma once

#include "node_graph_engine.h"
#include "signal_node.h"

class AdcEngine {
public:
    AdcEngine(int id, NodeGraphEngine& graph);

    int id() const { return m_id; }
    int graphNodeId() const { return m_graph_node_id; }
    int inputPinId() const;
    int outputPinId() const;

    void update(double dt);

    double fs_Hz() const { return m_fs_Hz; }
    void setFs_Hz(double fs) { m_fs_Hz = fs; }
    double nsd_dBm_per_Hz() const { return m_nsd_dBm_per_Hz; }
    void setNsd_dBm_per_Hz(double nsd) { m_nsd_dBm_per_Hz = nsd; }
    int bits() const { return m_bits; }
    void setBits(int b) { m_bits = b > 0 ? b : 1; }
    double v_fs() const { return m_v_fs; }
    void setVfs(double v) { m_v_fs = v; }

    SignalNode& node() { return m_node; }

private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph;
    SignalNode m_node;

    double m_fs_Hz = 1e9;
    double m_nsd_dBm_per_Hz = -155.0;
    int m_bits = 12;
    double m_v_fs = 2.0;
};
