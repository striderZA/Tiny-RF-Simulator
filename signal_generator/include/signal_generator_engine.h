#pragma once
#include "common.h"

class SignalGeneratorEngine {
  public:
    SignalGeneratorEngine(int id);

    int id() const { return m_id; }
    const tone &activeTone() const { return m_active_tone; }

    void setToneIndex(int idx) { m_active_tone.first = idx; }
    void setToneAmplitude(double dBm) { m_active_tone.second = dBm; }

  private:
    int m_id;
    tone m_active_tone;
};
