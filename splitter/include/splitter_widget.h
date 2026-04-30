#pragma once

#include "splitter_engine.h"
#include <functional>
#include <memory>
#include <vector>

class SplitterWidget {
  public:
    SplitterWidget(std::vector<std::unique_ptr<SplitterEngine>>& engines);
    void draw(const char* title, bool* p_open = nullptr);

    std::function<void()> onAddSplitter;
    std::function<void(size_t index)> onRemoveSplitter;

  private:
    std::vector<std::unique_ptr<SplitterEngine>>& m_engines;
};
