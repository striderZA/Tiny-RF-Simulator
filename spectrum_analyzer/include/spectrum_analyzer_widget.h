#pragma once
#include "spectrum_analyzer_engine.h"
#include "view_manager.h"

class SpectrumAnalyzerWidget {
  public:
    SpectrumAnalyzerWidget(SpectrumAnalyzerEngine &engine, ViewManager &vm);

    void draw(const char *title, bool *p_open = nullptr);
    bool peakDetectEnabled() const { return m_enable_peak_detect; }
    void enablePeakDetect(bool isEnabled) { m_enable_peak_detect = isEnabled; }

    std::vector<int> findPeaks(const std::vector<double> &power_dBm, double threshold_dBm);

  private:
    SpectrumAnalyzerEngine &m_engine;
    ViewManager &m_view_manager;
    bool m_enable_peak_detect;
};
