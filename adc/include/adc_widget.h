#pragma once

#include <functional>
#include <memory>
#include <vector>

class AdcEngine;

class AdcWidget {
public:
    explicit AdcWidget(std::vector<std::unique_ptr<AdcEngine>>& engines);

    void draw(const char* title, bool* p_open = nullptr);

    std::function<void()> onAddAdc;
    std::function<void(size_t)> onRemoveAdc;

private:
    std::vector<std::unique_ptr<AdcEngine>>& m_engines;
};
