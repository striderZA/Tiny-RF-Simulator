#pragma once
#include "common.h"
#include "signal_node.h"

class SignalGeneratorEngine {
  public:
    SignalGeneratorEngine(int id);

    int id() const { return m_id; }
    const tone &activeTone() const { return m_active_tone; }

    void setToneFrequency(double frequency) { m_active_tone.first = frequency; }
    void setToneAmplitude(double dBm) { m_active_tone.second = dBm; }
    void setFreqStep(double Hz) { m_f_step_Hz = Hz; rebuildFrequencyGrid(); }

    SignalNode &node() { return m_node; }
    void update(double dt);

    double f_step_Hz() const { return m_f_step_Hz; }

  private:
    int m_id;
    tone m_active_tone;
    SignalNode m_node;
    double m_f_step_Hz = 10e6;

    void rebuildFrequencyGrid();
};
