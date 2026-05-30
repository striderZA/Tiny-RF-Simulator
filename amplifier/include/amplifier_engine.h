#pragma once

#include "common.h"
#include "node_graph_engine.h"
#include "nonlinear_model.h"
#include "signal_node.h"
#include "component_interface.h"

class AmplifierEngine : public IComponentEngine {
  public:
    AmplifierEngine(int id, NodeGraphEngine& graph);
    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_node_id; }
    std::string hoverSummary() const override;
    int inputPinId() const override;
    int outputPinId() const override;

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
    void update(double dt) override;

    SignalNode &node() override { return m_node; }
    const SignalNode& node() const override { return m_node; }

    double gain_dB() const { return m_gain_dB; }
    double nf_dB() const { return m_nf_dB; }
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
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;

    SignalNode m_node;
    double m_gain_dB = 0.0;
    double m_nf_dB = 0.0;
    bool m_dirty = true;
    const Spectrum* m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;
    NonlinearModel m_nonlinear;
};
