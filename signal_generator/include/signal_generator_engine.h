#pragma once
#include "component_engine_base.h"
#include "node_graph_engine.h"
#include "signal_node.h"

class SignalGeneratorEngine : public ComponentEngineBase {
  public:
    SignalGeneratorEngine(int id, NodeGraphEngine &graph);

    std::string_view type_name() const override { return "generator"; }
    std::string hoverSummary() const override;

    void addTone(double freq_Hz, double power_dBm, double phase_deg = 0.0) {
        m_tones.push_back({freq_Hz, power_dBm, phase_deg});
        m_dirty = true;
    }
    void removeTone(size_t index) {
        if (index < m_tones.size()) {
            m_tones.erase(m_tones.begin() + static_cast<std::ptrdiff_t>(index));
            m_dirty = true;
        }
    }
    void updateTone(size_t index, double freq_Hz, double power_dBm, double phase_deg = 0.0) {
        if (index < m_tones.size()) {
            m_tones[index].freq_Hz = freq_Hz;
            m_tones[index].power_dBm = power_dBm;
            m_tones[index].phase_deg = phase_deg;
            m_dirty = true;
        }
    }
    const std::vector<Spectrum::Tone> &tones() const { return m_tones; }
    size_t toneCount() const { return m_tones.size(); }

    double fs_Hz() const { return m_fs_Hz; }
    void setFs_Hz(double fs) { m_fs_Hz = fs; }

    void update(double dt) override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json &) override;

  private:
    std::vector<Spectrum::Tone> m_tones;
    double m_fs_Hz = 0.0;

    void rebuildFrequencyGrid();
};
