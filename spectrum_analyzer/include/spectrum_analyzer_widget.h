#pragma once
#include "spectrum.h"
#include "spectrum_analyzer_engine.h"
#include "spectrum_analyzer_harmonics.h"
#include "spectrum_analyzer_markers.h"
#include "spectrum_analyzer_measurement.h"
#include "spectrum_analyzer_peak_table.h"
#include "view_manager.h"
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class PFBChannelizerEngine;

struct DragZoomState {
    bool active = false;
    double start_freq_Hz = 0.0;
    double end_freq_Hz = 0.0;
};

class SpectrumAnalyzerWidget {
  public:
    SpectrumAnalyzerWidget(SpectrumAnalyzerEngine &engine, const ViewManager &vm);

    void draw(const char *title, bool *p_open = nullptr);
    void setProbeLabels(const std::vector<std::string> &labels) { m_probe_labels = labels; }
    void setProbeTargets(const std::vector<std::pair<SignalNode *, int>> &targets);
    void setPFBs(const std::vector<PFBChannelizerEngine *> &pfbs);

    // The plot fills the window's remaining vertical space and never shrinks
    // below this floor (issue #88).
    static constexpr float kMinPlotHeight = 180.0f;

    // The jitter-free combined trace the last draw() used for the Peak text, snap-to-peak
    // and markers (issue #292). Empty when the last draw() had no visible trace.
    const std::vector<double> &readoutTrace() const { return m_readout_dBm; }
    // The combined axis readoutTrace() is sampled on (the Peak/marker axis).
    const std::vector<double> &readoutAxis() const { return m_readout_axis; }

    // Measurement source state: all visible traces, or one probe's trace. View state, not
    // project data; the resolution reports a hidden/removed fallback for the readout.
    const MeasurementSelection &measurementSelection() const { return m_measurement; }
    const MeasurementSelection::Resolution &measurementResolution() const {
        return m_measurement_resolution;
    }

    // Marker view state: four markers, M1 is the reference. Not project data.
    const MarkerModel &markers() const { return m_markers; }
    std::string markerSessionState() const { return m_markers.serialize(); }
    bool setMarkerSessionState(const std::string &token) { return m_markers.restore(token); }

    // Peak table view state: up to five strongest peaks of the measurement source.
    // Not project data; its visibility persists with the panel like the marker table.
    const PeakTableModel &peakTable() const { return m_peaks; }
    void setPeakTableVisible(bool visible) { m_peaks.setVisible(visible); }

    // Harmonic measurement view state: the fundamental and its 2nd/3rd harmonics on the
    // measurement source. Not project data; its visibility persists like the peak table.
    const HarmonicModel &harmonics() const { return m_harmonics; }
    void setHarmonicsVisible(bool visible) { m_harmonics.setVisible(visible); }

    // The pixel rect of the plot's data area in the last draw(); test seam for
    // frequency-to-pixel mapping in the headless UI test.
    struct PlotArea {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };
    PlotArea lastPlotArea() const { return m_last_plot_area; }

  private:
    SpectrumAnalyzerEngine &m_engine;
    const ViewManager &m_view_manager;
    std::vector<std::string> m_probe_labels;
    std::vector<std::pair<SignalNode *, int>> m_probe_targets;  // probe-pin order
    std::unordered_map<SignalNode *, int> m_probe_output_index; // node -> probed output port
    MarkerModel m_markers;
    PeakTableModel m_peaks;
    HarmonicModel m_harmonics;
    DragZoomState m_zoom;
    int m_drag_marker = -1;
    std::vector<PFBChannelizerEngine *> m_pfb_ptrs;
    std::unordered_map<SignalNode *, PFBChannelizerEngine *> m_pfb_map;
    std::vector<bool> m_trace_visible;  // Track visibility for each probe trace
    std::vector<double> m_readout_dBm;  // readout trace of the last draw() (issue #292)
    std::vector<double> m_readout_axis; // combined axis of the last draw()
    PlotArea m_last_plot_area;

    MeasurementSelection m_measurement;
    MeasurementSelection::Resolution m_measurement_resolution;
    std::string m_measure_label; // label of the chosen probe, kept for the removed fallback
    bool m_measure_source_known = false;
    bool m_last_measure_all_visible = true;
    MeasurementProbeId m_last_measure_probe;

    const Spectrum &outputSpectrumFor(SignalNode *node) const;
    std::vector<MeasurementProbeId> measurementProbeIds() const;
    const Spectrum *spectrumForProbeSlot(size_t slot) const;

    // Vertical space the marker controls, readout strip, and optional table need.
    float markerAreaReserve() const;
    bool drawMarkersOnPlot(const std::vector<double> &freq_axis,
                           const std::vector<double> &display_dBm);
    void drawMarkerControls(const std::vector<double> &freq_axis,
                            const std::vector<double> &display_dBm);
    void drawMarkerTable(const std::vector<double> &freq_axis,
                         const std::vector<double> &display_dBm);
    void drawPeakTable();
    void drawHarmonicTable();
    // Moves the selected marker onto the selected table row, enabling it first if needed.
    void applySelectedPeak();
};
