#pragma once

#include "s_parameter_amplifier_engine.h"
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

class SParameterAmplifierWidget {
  public:
    SParameterAmplifierWidget(std::vector<std::unique_ptr<SParameterAmplifierEngine>>& engines);
    void draw(const char* title, bool* p_open = nullptr);

    std::function<void()> onAddSParamAmp;
    std::function<void(size_t index)> onRemoveSParamAmp;

  private:
    std::vector<std::unique_ptr<SParameterAmplifierEngine>>& m_engines;
    // per-engine-index, per-parameter visibility
    std::unordered_map<size_t, std::vector<bool>> m_param_visible;
};
