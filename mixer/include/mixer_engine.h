#pragma once

#include <string>

#include "component_engine_base.h"
#include "signal_node.h"

class MixerEngine : public ComponentEngineBase {
  public:
    MixerEngine(int id, NodeGraphEngine &graph);
    std::string_view type_name() const override { return "mixer"; }
    std::string hoverSummary() const override;

    void setLoFreq_Hz(double f) {
        if (f != m_lo_freq_Hz) {
            m_lo_freq_Hz = f;
            m_dirty = true;
        }
    }
    void setConversionGain_dB(double g) {
        if (g != m_conv_gain_dB) {
            m_conv_gain_dB = g;
            m_dirty = true;
        }
    }
    void setNF_dB(double nf) {
        if (nf != m_nf_dB) {
            m_nf_dB = nf;
            m_dirty = true;
        }
    }

    double loFreq_Hz() const { return m_lo_freq_Hz; }
    double conversionGain_dB() const { return m_conv_gain_dB; }
    double nf_dB() const { return m_nf_dB; }

    void update(double dt) override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json &) override;

  private:
    double m_lo_freq_Hz = 1e9;
    double m_conv_gain_dB = -6.0;
    double m_nf_dB = 0.0;
};
