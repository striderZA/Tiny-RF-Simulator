#pragma once

#include <string>

#include "common.h"
#include "node_graph_engine.h"
#include "signal_node.h"

class MixerEngine {
  public:
    MixerEngine(int id, NodeGraphEngine& graph);
    int id() const { return m_id; }
    int graphNodeId() const { return m_graph_node_id; }
    std::string hoverSummary() const;
    int inputPinId() const;
    int outputPinId() const;

    void setLoFreq_Hz(double f) {
        if (f != m_lo_freq_Hz) { m_lo_freq_Hz = f; m_dirty = true; }
    }
    void setConversionGain_dB(double g) {
        if (g != m_conv_gain_dB) { m_conv_gain_dB = g; m_dirty = true; }
    }
    void setNF_dB(double nf) {
        if (nf != m_nf_dB) { m_nf_dB = nf; m_dirty = true; }
    }

    double loFreq_Hz() const { return m_lo_freq_Hz; }
    double conversionGain_dB() const { return m_conv_gain_dB; }
    double nf_dB() const { return m_nf_dB; }

    void update(double dt);

    SignalNode& node() { return m_node; }

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;

    SignalNode m_node;
    double m_lo_freq_Hz = 1e9;
    double m_conv_gain_dB = -6.0;
    double m_nf_dB = 0.0;
    bool m_dirty = true;
    uint64_t m_cached_input_generation = 0;
};
