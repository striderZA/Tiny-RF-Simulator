#pragma once
#include "spectrum_analyzer_engine.h"
#include "spectrum.h"
#include "view_manager.h"
#include <string>
#include <vector>

struct MarkerState {
    bool enabled = false;
    double target_freq_Hz = 0.0;   // Frequency the marker aims for
    bool is_dragging = false;      // True while mouse is held down on plot
    std::vector<Peak> peaks;       // Cached peaks from last snap
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

    int resolveMarkerIdx(const std::vector<double> &freq_axis,
                         const std::vector<double> &data) const;
    void drawMarkerOnPlot(double freq_Hz, double power_dBm);
    void drawMarkerControls(const std::vector<double> &freq_axis,
                            const std::vector<double> &display_dBm);
};
