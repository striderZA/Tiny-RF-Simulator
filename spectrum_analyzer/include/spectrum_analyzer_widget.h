#pragma once
#include "spectrum_analyzer_engine.h"

class SpectrumAnalyzerWidget {
  public:
    SpectrumAnalyzerWidget(SpectrumAnalyzerEngine &engine);

    void draw(const char *title, bool *p_open = nullptr);

  private:
    SpectrumAnalyzerEngine &m_engine;
};
