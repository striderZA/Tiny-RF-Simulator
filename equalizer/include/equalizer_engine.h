#pragma once

#include "component_engine_base.h"
#include "node_graph_engine.h"
#include "s_parameter_data.h"
#include "signal_node.h"
#include <algorithm>
#include <string>

class EqualizerEngine : public ComponentEngineBase {
  public:
    EqualizerEngine(int id, NodeGraphEngine &graph);

    std::string_view type_name() const override { return "equalizer"; }
    std::string hoverSummary() const override;
    void update(double dt) override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json &) override;

    // Ideal mode parameters
    void setRefGain_dB(double g) {
        m_ref_gain_dB = g;
        m_dirty = true;
    }
    double refGain_dB() const { return m_ref_gain_dB; }
    void setRefFreq_Hz(double f) {
        m_ref_freq_Hz = std::max(f, 1.0);
        m_dirty = true;
    }
    double refFreq_Hz() const { return m_ref_freq_Hz; }
    void setSlope_dBPerDecade(double s) {
        m_slope_dB_per_decade = s;
        m_dirty = true;
    }
    double slope_dBPerDecade() const { return m_slope_dB_per_decade; }

    // S-param mode
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
    // Ideal mode
    double m_ref_gain_dB = 0.0;
    double m_ref_freq_Hz = 1e9;
    double m_slope_dB_per_decade = 0.0;

    // S-param mode
    SParameterData m_sparam_data;
    std::string m_sparam_filepath;
    bool m_sparam_mode = false;
    int m_sparam_fwd_idx = 0;
    const Spectrum *m_cached_sparam_input = nullptr;
    uint64_t m_cached_sparam_generation = 0;
};
