#pragma once
#include "common.h"
#include "node_graph_engine.h"
#include "signal_node.h"

class SignalGeneratorEngine {
  public:
    SignalGeneratorEngine(int id, NodeGraphEngine& graph);

    int id() const { return m_id; }
    int graphNodeId() const { return m_graph_node_id; }
    int outputPinId() const;

    void addTone(double freq_Hz, double power_dBm, double phase_deg = 0.0);
    void removeTone(size_t index);
    void updateTone(size_t index, double freq_Hz, double power_dBm, double phase_deg = 0.0);
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

    void rebuildFrequencyGrid();
};
