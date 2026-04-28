#pragma once
#include "common.h"
#include "signal_node.h"

class SignalGeneratorEngine {
  public:
    SignalGeneratorEngine(int id);

    int id() const { return m_id; }

    void addTone(double freq_Hz, double power_dBm);
    void removeTone(size_t index);
    void updateTone(size_t index, double freq_Hz, double power_dBm);
    const std::vector<Spectrum::Tone> &tones() const { return m_tones; }
    size_t toneCount() const { return m_tones.size(); }

    SignalNode &node() { return m_node; }
    void update(double dt);

  private:
    int m_id;
    std::vector<Spectrum::Tone> m_tones;
    SignalNode m_node;

    void rebuildFrequencyGrid();
};
