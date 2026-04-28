#pragma once
#include "common.h"
#include "signal_node.h"

class SignalGeneratorEngine {
  public:
    SignalGeneratorEngine(int id);

    int id() const { return m_id; }
    const tone &activeTone() const { return m_active_tone; }

    void setToneFrequency(double frequency);
    void setToneAmplitude(double dBm) { m_active_tone.second = dBm; }

    SignalNode &node() { return m_node; }
    void update(double dt);

  private:
    int m_id;
    tone m_active_tone;
    SignalNode m_node;
};
