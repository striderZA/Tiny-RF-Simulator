#pragma once

#include "common.h"
#include "node_graph_engine.h"
#include "signal_node.h"

class MixerEngine {
  public:
    MixerEngine(int id, NodeGraphEngine& graph);
    int id() const { return m_id; }
    int graphNodeId() const { return m_graph_node_id; }
    int inputPinId() const;
    int outputPinId() const;

    void setLoFreq_Hz(double f) { m_lo_freq_Hz = f; }
    void setConversionGain_dB(double g) { m_conv_gain_dB = g; }

    double loFreq_Hz() const { return m_lo_freq_Hz; }
    double conversionGain_dB() const { return m_conv_gain_dB; }

    void update(double dt);

    SignalNode& node() { return m_node; }

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;

    SignalNode m_node;
    double m_lo_freq_Hz = 1e9;
    double m_conv_gain_dB = -6.0;
};
