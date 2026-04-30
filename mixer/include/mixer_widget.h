#pragma once

#include "mixer_engine.h"
#include <functional>
#include <memory>
#include <vector>

class MixerWidget {
  public:
    MixerWidget(std::vector<std::unique_ptr<MixerEngine>>& engines);
    void draw(const char* title, bool* p_open = nullptr);

    std::function<void()> onAddMixer;
    std::function<void(size_t index)> onRemoveMixer;

  private:
    std::vector<std::unique_ptr<MixerEngine>>& m_engines;
};
