#pragma once
#include "common.h"
#include "node_graph_engine.h"
#include "signal_node.h"
#include <nlohmann/json.hpp>

class SignalGeneratorEngine {
  public:
    SignalGeneratorEngine(int id, NodeGraphEngine& graph);

    int id() const { return m_id; }
    int graphNodeId() const { return m_graph_node_id; }
    std::string hoverSummary() const;
    void serialize(nlohmann::json& j) const;
    void deserialize(const nlohmann::json& j);
    bool isDirty() const { return m_dirty; }
    int outputPinId() const;

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

    SignalNode &node() { return m_node; }
    void update(double dt);

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;
    std::vector<Spectrum::Tone> m_tones;
    SignalNode m_node;
    bool m_dirty = true;

    void rebuildFrequencyGrid();
};
