#pragma once

#include "common.h"
#include "component_interface.h"
#include "node_graph_engine.h"
#include "nonlinear_model.h"
#include "signal_node.h"
#include "s_parameter_data.h"
#include <string>

class SParamEngine : public IComponentEngine {
public:
    SParamEngine(int id, NodeGraphEngine& graph, const std::string& filepath);

    // IComponentEngine
    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_node_id; }
    std::string hoverSummary() const override;
    int inputPinId() const override { return inputPinId(0); }
    int outputPinId() const override { return outputPinId(0); }
    SignalNode& node() override { return m_node; }
    const SignalNode& node() const override { return m_node; }
    void update(double dt) override;

    // Multi-port API
    int numPorts() const { return m_data.numPorts(); }
    int inputPinId(int port) const override;
    int outputPinId(int port) const override;
    bool fullMatrixMode() const { return m_forward_param_idx < 0; }
    void setFullMatrixMode(bool full) {
        int new_idx = full ? -1 : (m_data.numPorts() > 1 ? m_data.numPorts() : 0);
        if (new_idx != m_forward_param_idx) {
            m_forward_param_idx = new_idx;
            m_dirty = true;
        }
    }

    void reload(const std::string& filepath);

    // S-parameter access
    const std::string& filepath() const { return m_filepath; }
    bool loaded() const { return m_data.loaded(); }
    const SParameterData& data() const { return m_data; }
    int forwardParamIdx() const { return m_forward_param_idx; }
    void setForwardParamIdx(int idx);

    // Optional: noise figure (0.0 = off/passive)
    double nf_dB() const { return m_nf_dB; }
    void setNF_dB(double nf) {
        if (nf != m_nf_dB) { m_nf_dB = nf; m_dirty = true; }
    }

    // Optional: nonlinearity (disabled by default)
    bool enableNonlinear() const { return m_nonlinear.enabled(); }
    double oip2_dBm() const { return m_nonlinear.oip2_dBm(); }
    double oip3_dBm() const { return m_nonlinear.oip3_dBm(); }
    void setEnableNonlinear(bool en) {
        if (en != m_nonlinear.enabled()) {
            m_nonlinear.setEnabled(en);
            m_dirty = true;
        }
    }
    void setOIP2_dBm(double oip2) {
        m_nonlinear.setOIP2_dBm(oip2);
        if (m_nonlinear.enabled()) m_dirty = true;
    }
    void setOIP3_dBm(double oip3) {
        m_nonlinear.setOIP3_dBm(oip3);
        if (m_nonlinear.enabled()) m_dirty = true;
    }

private:
    void rebuildNode();

    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;
    SignalNode m_node;
    SParameterData m_data;
    std::string m_filepath;
    int m_forward_param_idx = 0;
    bool m_dirty = true;
    bool m_cache_valid = false;
    std::vector<const Spectrum*> m_cached_input_ptrs;
    std::vector<uint64_t> m_cached_input_generations;

    double m_nf_dB = 0.0;
    NonlinearModel m_nonlinear;
};
