#pragma once
#include "spectrum_analyzer_engine.h"
#include "spectrum.h"
#include "view_manager.h"
#include <string>
#include <utility>
#include <vector>

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

struct TraceCacheEntry {
    const Spectrum* spectrum_ptr = nullptr;
    uint64_t spectrum_gen = 0;
    double rbw = 0;
    double vbw = 0;
    bool jitter_enabled = false;
    double jitter_sigma = 0;
    std::vector<double> data;
};

struct CombinedCacheKey {
    std::vector<std::pair<const Spectrum*, uint64_t>> inputs;
    double rbw = 0;
    double vbw = 0;
    bool jitter_enabled = false;
    double jitter_sigma = 0;
};

class SpectrumAnalyzerWidget {
  public:
    SpectrumAnalyzerWidget(SpectrumAnalyzerEngine &engine, ViewManager &vm);

    void draw(const char *title, bool *p_open = nullptr);
    void setProbeLabels(const std::vector<std::string>& labels) { m_probe_labels = labels; }

  private:
    SpectrumAnalyzerEngine &m_engine;
    ViewManager &m_view_manager;
    std::vector<std::string> m_probe_labels;
    MarkerState m_marker;
    DragZoomState m_zoom;

    mutable std::vector<TraceCacheEntry> m_trace_cache;
    mutable std::vector<double> m_combined_cache;
    mutable CombinedCacheKey m_combined_key;
    mutable double m_avg_noise_cache = -174.0;

    bool traceCacheValid(size_t idx, const Spectrum& spec) const;
    bool combinedCacheValid(const std::vector<const Spectrum*>& specs) const;

    int resolveMarkerIdx(const std::vector<double> &freq_axis,
                         const std::vector<double> &data) const;
    void drawMarkerOnPlot(double freq_Hz, double power_dBm);
    void drawMarkerControls(const std::vector<double> &freq_axis,
                            const std::vector<double> &display_dBm);
};
