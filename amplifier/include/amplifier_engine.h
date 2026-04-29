#pragma once

#include "common.h"
#include "node_graph_engine.h"
#include "signal_node.h"

class AmplifierEngine {
  public:
    AmplifierEngine(int id, NodeGraphEngine& graph);
    int id() const { return m_id; }
    int graphNodeId() const { return m_graph_node_id; }
    int inputPinId() const;
    int outputPinId() const;

    void setGain_dB(double g) { m_gain_dB = g; }
    void setNF_dB(double nf) { m_nf_dB = nf; }
    void update(double dt);

    SignalNode &node() { return m_node; }

    double gain_dB() const { return m_gain_dB; }
    double nf_dB() const { return m_nf_dB; }

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;

    SignalNode m_node;
    double m_gain_dB = 0.0;
    double m_nf_dB = 0.0;
};
