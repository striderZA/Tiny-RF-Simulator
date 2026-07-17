#pragma once
#include "spectrum_analyzer_engine.h"
#include "spectrum.h"
#include "view_manager.h"
#include <string>
#include <unordered_map>
#include <vector>

class PFBChannelizerEngine;

struct MarkerState {
    bool enabled = false;
    double target_freq_Hz = 0.0;
    bool is_dragging = false;
    std::vector<Peak> peaks;
};

struct DragZoomState {
    bool active = false;
    double start_freq_Hz = 0.0;
    double end_freq_Hz = 0.0;
};

class SpectrumAnalyzerWidget {
  public:
    SpectrumAnalyzerWidget(SpectrumAnalyzerEngine &engine, ViewManager &vm);

    void draw(const char *title, bool *p_open = nullptr);
    void setProbeLabels(const std::vector<std::string>& labels) { m_probe_labels = labels; }
    void setPFBs(const std::vector<PFBChannelizerEngine*>& pfbs);

  private:
    SpectrumAnalyzerEngine &m_engine;
    ViewManager &m_view_manager;
    std::vector<std::string> m_probe_labels;
    MarkerState m_marker;
    DragZoomState m_zoom;
    std::vector<PFBChannelizerEngine*> m_pfb_ptrs;
    std::unordered_map<SignalNode*, PFBChannelizerEngine*> m_pfb_map;
    std::vector<bool> m_trace_visible;  // Track visibility for each probe trace

    int resolveMarkerIdx(const std::vector<double> &freq_axis,
                         const std::vector<double> &data) const;
    void drawMarkerOnPlot(double freq_Hz, double power_dBm);
    void drawMarkerControls(const std::vector<double> &freq_axis,
                            const std::vector<double> &display_dBm);
};
