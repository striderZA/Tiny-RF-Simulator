#pragma once

#include "amplifier_engine.h"

class AmplifierWidget {
  public:
    AmplifierWidget(AmplifierEngine &engine);

    void draw(const char *title, bool *p_open = nullptr);

  private:
    AmplifierEngine &m_engine;
};
