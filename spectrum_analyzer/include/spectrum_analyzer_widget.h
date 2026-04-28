#pragma once
#include "spectrum_analyzer_engine.h"
#include "view_manager.h"
#include <string>

class SpectrumAnalyzerWidget {
  public:
    SpectrumAnalyzerWidget(SpectrumAnalyzerEngine &engine, ViewManager &vm);

    void draw(const char *title, bool *p_open = nullptr);
    void setProbeLabel(const std::string& label) { m_probe_label = label; }

  private:
    SpectrumAnalyzerEngine &m_engine;
    ViewManager &m_view_manager;
    std::string m_probe_label;
};
