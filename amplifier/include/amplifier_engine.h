#pragma once

#include "component_interface.h"
#include "node_graph_engine.h"
#include "nonlinear_model.h"
#include "s_parameter_data.h"
#include "signal_node.h"
#include <algorithm>
#include <utility>
#include <vector>

class AmplifierEngine : public IComponentEngine {
  public:
    AmplifierEngine(int id, NodeGraphEngine &graph);
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
        double clamped = std::max(0.0, nf);
        if (clamped != m_nf_dB) {
            m_nf_dB = clamped;
            m_dirty = true;
        }
    }
    void setNfCurve(const nlohmann::json &curve) {
        std::vector<std::pair<double, double>> next;
        next.reserve(curve.size());
        for (const auto &point : curve) {
            if (!point.is_array() || point.size() != 2 || !point[0].is_number() ||
                !point[1].is_number())
                continue;
            next.emplace_back(point[0].get<double>(), std::max(0.0, point[1].get<double>()));
        }
        if (next != m_nf_curve) {
            m_nf_curve = std::move(next);
            m_dirty = true;
        }
    }
    void update(double dt) override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json &) override;

    SignalNode &node() override { return m_node; }
    const SignalNode &node() const override { return m_node; }

    double gain_dB() const { return m_gain_dB; }
    double nf_dB() const { return m_nf_dB; }
    bool hasNfCurve() const { return !m_nf_curve.empty(); }
    bool enableNonlinear() const { return m_nonlinear.enabled(); }
    double oip2_dBm() const { return m_nonlinear.oip2_dBm(); }
    double oip3_dBm() const { return m_nonlinear.oip3_dBm(); }
    double p1db_dBm() const { return m_nonlinear.p1db_dBm(); }

    void setEnableNonlinear(bool en) {
        if (en != m_nonlinear.enabled()) {
            m_nonlinear.setEnabled(en);
            m_dirty = true;
        }
    }
    void setOIP2_dBm(double oip2) {
        m_nonlinear.setOIP2_dBm(std::max(-30.0, oip2));
        if (m_nonlinear.enabled())
            m_dirty = true;
    }
    void setOIP3_dBm(double oip3) {
        m_nonlinear.setOIP3_dBm(std::max(-30.0, oip3));
        if (m_nonlinear.enabled())
            m_dirty = true;
    }
    void setP1dB_dBm(double p1db) {
        m_nonlinear.setP1dB_dBm(std::max(-30.0, p1db));
        if (m_nonlinear.enabled())
            m_dirty = true;
    }

    // S-parameter mode
    void setSParamFilepath(const std::string &path);
    bool sparamMode() const { return m_sparam_mode; }
    void setSParamMode(bool en) {
        m_sparam_mode = en;
        m_dirty = true;
    }
    bool sparamLoaded() const { return m_sparam_data.loaded(); }
    const std::string &sparamFilepath() const { return m_sparam_filepath; }
    const SParameterData &sparamData() const { return m_sparam_data; }

  private:
    int m_id;

    double interpolatedNf_dB(double freq_Hz) const;
    int m_graph_node_id = -1;
    NodeGraphEngine *m_graph = nullptr;

    SignalNode m_node;
    double m_gain_dB = 0.0;
    double m_nf_dB = 0.0;
    std::vector<std::pair<double, double>> m_nf_curve;
    bool m_dirty = true;
    const Spectrum *m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;
    NonlinearModel m_nonlinear;

    // S-parameter state
    SParameterData m_sparam_data;
    std::string m_sparam_filepath;
    bool m_sparam_mode = false;
    int m_sparam_fwd_idx = 0;
    const Spectrum *m_cached_sparam_input = nullptr;
    uint64_t m_cached_sparam_generation = 0;
};
