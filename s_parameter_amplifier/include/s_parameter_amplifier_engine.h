#pragma once

#include "common.h"
#include "node_graph_engine.h"
#include "signal_node.h"
#include "s_parameter_data.h"
#include <string>

class SParameterAmplifierEngine {
  public:
    SParameterAmplifierEngine(int id, NodeGraphEngine& graph, const std::string& filepath);
    int id() const { return m_id; }
    int graphNodeId() const { return m_graph_node_id; }
    std::string hoverSummary() const;
    int inputPinId() const;
    int outputPinId() const;

    void update(double dt);
    void reload(const std::string& filepath);

    SignalNode& node() { return m_node; }
    const std::string& filepath() const { return m_filepath; }
    bool loaded() const { return m_data.loaded(); }

    int numPorts() const { return m_data.numPorts(); }
    const std::vector<double>& freqs() const { return m_data.freqs(); }
    const std::vector<std::vector<std::complex<double>>>& params() const { return m_data.params(); }
    int forwardParamIdx() const { return m_forward_param_idx; }
    void setForwardParamIdx(int idx);

    double nf_dB() const { return m_nf_dB; }
    void setNF_dB(double nf) {
        if (nf != m_nf_dB) { m_nf_dB = nf; m_dirty = true; }
    }

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
        if (oip2 != m_oip2_dBm) { m_oip2_dBm = oip2; if (m_enable_nonlinear) { recomputeCoefficients(); m_dirty = true; } }
    }
    void setOIP3_dBm(double oip3) {
        if (oip3 != m_oip3_dBm) { m_oip3_dBm = oip3; if (m_enable_nonlinear) { recomputeCoefficients(); m_dirty = true; } }
    }

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;

    SignalNode m_node;
    SParameterData m_data;
    std::string m_filepath;
    int m_forward_param_idx = 0;
    bool m_dirty = true;
    const Spectrum* m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;

    double m_nf_dB = 0.0;
    bool m_enable_nonlinear = false;
    double m_oip2_dBm = 100.0;
    double m_oip3_dBm = 100.0;
    double m_k1 = 0.0;
    double m_k2 = 0.0;

    void recomputeCoefficients();
};