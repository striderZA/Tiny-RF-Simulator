#pragma once
#include "spectrum_analyzer_engine.h"
#include "spectrum.h"
#include "view_manager.h"
#include <string>
#include <vector>

struct MarkerState {
    bool enabled = false;
    int selected_peak_idx = -1;   // -1 = manual, >=0 = snapped to peak
    int manual_bin = 0;
    std::vector<Peak> peaks;
};

struct MarkerInfo {
    int idx = 0;
    double freq_Hz = 0.0;
    double power_dBm = -174.0;
};

class SpectrumAnalyzerWidget {
  public:
    SpectrumAnalyzerWidget(SpectrumAnalyzerEngine &engine, ViewManager &vm);

    void draw(const char *title, bool *p_open = nullptr);
    void setProbeLabel(const std::string& label) { m_probe_label = label; }

  private:
    SpectrumAnalyzerEngine &m_engine;
    ViewManager &m_view_manager;
    std::string m_probe_label;
    MarkerState m_marker;

    MarkerInfo resolveMarker(const std::vector<double> &freq_axis,
                             const std::vector<double> &power_dBm) const;
    void drawMarkerOnPlot(const MarkerInfo &info);
    void drawMarkerControls(const std::vector<double> &freq_axis,
                            const std::vector<double> &display_dBm);
};
