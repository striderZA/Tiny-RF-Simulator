#pragma once

#include "imgui.h"

namespace ui {

// A button drawn with the destructive-action tokens: fill #3A1A1A, hover #4A2020, active
// #5A2626, text #FF8A80 (#FF8A80 on #3A1A1A measures 6.85:1, WCAG 2.1). The style push is scoped
// to the call, so the caller's colours are restored before it returns.
bool dangerButton(const char *label, const ImVec2 &size = ImVec2(0, 0));
bool dangerSmallButton(const char *label);

} // namespace ui
