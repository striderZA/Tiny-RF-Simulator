#pragma once

#include "amp_file.h"
#include "component_engine_base.h"
#include "node_graph_engine.h"
#include "nonlinear_model.h"
#include "s_parameter_data.h"
#include "signal_node.h"
#include <algorithm>
#include <optional>

class AmplifierEngine : public ComponentEngineBase {
  public:
    AmplifierEngine(int id, NodeGraphEngine &graph);
    std::string_view type_name() const override { return "amplifier"; }
    std::string hoverSummary() const override;

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
    void update(double dt) override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json &) override;

    double gain_dB() const { return m_gain_dB; }
    double nf_dB() const { return m_nf_dB; }
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
        m_sparam_mode = en && (!m_sparam_data.loaded() || m_sparam_data.numPorts() >= 2);
        m_dirty = true;
    }
    bool sparamLoaded() const { return m_sparam_data.loaded(); }
    const std::string &sparamFilepath() const { return m_sparam_filepath; }
    const SParameterData &sparamData() const { return m_sparam_data; }

    // AMP intercept file (MathWorks AMP, IIP3/OIP3 sections). While a file is loaded, its
    // curves replace the scalar OIP3. A file that fails to load keeps its path and error,
    // and the scalar OIP3 applies.
    void setAmpFilepath(const std::string &path);
    const std::string &ampFilepath() const { return m_amp_filepath; }
    bool ampLoaded() const { return m_amp.has_value(); }
    const AmpFile *ampData() const { return m_amp ? &*m_amp : nullptr; }
    const std::string &ampError() const { return m_amp_error; }

  private:
    double m_gain_dB = 0.0;
    double m_nf_dB = 0.0;
    NonlinearModel m_nonlinear;

    // S-parameter state
    SParameterData m_sparam_data;
    std::string m_sparam_filepath;
    bool m_sparam_mode = false;
    int m_sparam_fwd_idx = 0;
    const Spectrum *m_cached_sparam_input = nullptr;
    uint64_t m_cached_sparam_generation = 0;

    // AMP intercept state
    std::string m_amp_filepath;
    std::optional<AmpFile> m_amp;
    std::string m_amp_error;

    void loadAmpFile(const std::string &path);
    NonlinearModel::Oip3Fn intercept3Function(const NonlinearModel::GainFn &gain) const;
};
