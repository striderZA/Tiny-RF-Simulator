#pragma once

#include "common.h"
#include "node_graph_engine.h"
#include "signal_node.h"
#include <nlohmann/json.hpp>

class AmplifierEngine {
  public:
    AmplifierEngine(int id, NodeGraphEngine& graph);
    int id() const { return m_id; }
    int graphNodeId() const { return m_graph_node_id; }
    std::string hoverSummary() const;
    int inputPinId() const;
    int outputPinId() const;
    void serialize(nlohmann::json& j) const;
    void deserialize(const nlohmann::json& j);
    bool isDirty() const { return m_dirty; }

    void setGain_dB(double g) {
        if (g != m_gain_dB) {
            m_gain_dB = g;
            m_dirty = true;
        }
    }
    void setNF_dB(double nf) {
        if (nf != m_nf_dB) {
            m_nf_dB = nf;
            m_dirty = true;
        }
    }
    void update(double dt);

    SignalNode &node() { return m_node; }

    double gain_dB() const { return m_gain_dB; }
    double nf_dB() const { return m_nf_dB; }
    bool enableNonlinear() const { return m_enable_nonlinear; }
    double oip2_dBm() const { return m_oip2_dBm; }
    double oip3_dBm() const { return m_oip3_dBm; }

    void setEnableNonlinear(bool en) {
        if (en != m_enable_nonlinear) {
            m_enable_nonlinear = en;
            m_dirty = true;
            if (en) recomputeCoefficients();
        }
    }
    void setOIP2_dBm(double oip2) {
        if (oip2 != m_oip2_dBm) {
            m_oip2_dBm = oip2;
            if (m_enable_nonlinear) { recomputeCoefficients(); m_dirty = true; }
        }
    }
    void setOIP3_dBm(double oip3) {
        if (oip3 != m_oip3_dBm) {
            m_oip3_dBm = oip3;
            if (m_enable_nonlinear) { recomputeCoefficients(); m_dirty = true; }
        }
    }

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;

    SignalNode m_node;
    double m_gain_dB = 0.0;
    double m_nf_dB = 0.0;
    bool m_dirty = true;
    const Spectrum* m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;
    bool m_enable_nonlinear = false;
    double m_oip2_dBm = 100.0;
    double m_oip3_dBm = 100.0;
    double m_k1 = 0.0;
    double m_k2 = 0.0;

    void recomputeCoefficients();
};
