#pragma once

#include "amplifier_engine.h"
#include <functional>
#include <memory>
#include <vector>

class AmplifierWidget {
  public:
    AmplifierWidget(std::vector<std::unique_ptr<AmplifierEngine>> &engines);

    void draw(const char *title, bool *p_open = nullptr);

    std::function<void()> onAddAmplifier;
    std::function<void(size_t)> onRemoveAmplifier;

  private:
    std::vector<std::unique_ptr<AmplifierEngine>> &m_engines;
};
