#pragma once
#include "spectrum_analyzer_engine.h"
#include "view_manager.h"
class SpectrumAnalyzerWidget {
  public:
    SpectrumAnalyzerWidget(SpectrumAnalyzerEngine &engine, ViewManager &vm);

    void draw(const char *title, bool *p_open = nullptr);

  private:
    SpectrumAnalyzerEngine &m_engine;
    ViewManager &m_view_manager;
};
