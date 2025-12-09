#pragma once

#include "imgui.h"
#include "logging_core.h"

class LoggingWidget {
  public:
    void draw(const char *title = "Log", bool *p_open = nullptr);

  private:
    bool auto_scroll = true;
    bool show_info = true;
    bool show_warn = true;
    bool show_error = true;

    ImGuiTextFilter filter;
};
