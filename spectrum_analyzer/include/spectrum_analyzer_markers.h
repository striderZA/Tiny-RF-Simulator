#pragma once
#include "spectrum.h"
#include <array>
#include <string>
#include <vector>

// Marker view state for the Spectrum Analyzer. This class is UI-free: it owns the marker
// positions, selection, delta flags, per-marker peak caches, and the session token, while the
// widget owns presentation. Markers are display cursors, never project data.
//
// M1 (index 0) is the reference marker. A marker in delta mode reads its frequency and amplitude
// offset from M1; M1 itself always reads absolute. Every value resolves on the combined readout
// axis the widget passes in (the jitter-free visible-trace sum the Peak readout uses).
class MarkerModel {
  public:
    static constexpr int kMaxMarkers = 4;
    static constexpr int kReferenceIndex = 0;

    struct Readout {
        bool enabled = false;            // the marker is switched on
        bool valid = false;              // enabled and the axis had a bin for it
        bool delta = false;              // this marker reads relative to M1
        bool reference_valid = false;    // delta mode and M1 has a bin
        double frequency_Hz = 0.0;       // absolute bin frequency
        double amplitude_dBm = 0.0;      // absolute bin amplitude
        double delta_frequency_Hz = 0.0; // marker minus reference
        double delta_amplitude_dB = 0.0;
    };

    int selected() const { return m_selected; }
    void select(int index);
    bool enabled(int index) const;
    void setEnabled(int index, bool enabled);
    bool delta(int index) const;
    // Ignored for the reference marker, which always reads absolute.
    void setDelta(int index, bool delta);
    double frequency(int index) const;
    // Stores a raw frequency; the readout resolves it to the nearest axis bin.
    void place(int index, double frequency_Hz);
    int enabledCount() const;
    bool anyEnabled() const;

    bool tableVisible() const { return m_table_visible; }
    void setTableVisible(bool visible) { m_table_visible = visible; }

    // Nearest bin on the shared readout axis, or -1 when the marker is off or the axis is empty.
    int resolveIndex(int index, const std::vector<double> &freq_axis,
                     const std::vector<double> &data) const;
    Readout readout(int index, const std::vector<double> &freq_axis,
                    const std::vector<double> &data) const;

    // Snap: replaces this marker's cached peak list and places it on the strongest peak.
    // An empty list leaves both the position and the cache unchanged.
    void snapToPeak(int index, const std::vector<Peak> &peaks);
    // Cycles through this marker's cached peaks, wrapping at the end. With an empty cache the
    // supplied list is adopted first (placing the marker on its first, strongest peak); with
    // neither a cache nor a supplied list nothing moves. Returns true only when it moved.
    bool nextPeak(int index, const std::vector<Peak> &peaks = {});
    const std::vector<Peak> &peaks(int index) const;

    // Session token: monotonic view state for SessionState persistence. Peaks are transient
    // and are not part of it. restore() rejects a malformed token without changing anything.
    std::string serialize() const;
    bool restore(const std::string &token);

  private:
    struct Marker {
        bool enabled = false;
        bool delta = false;
        double frequency_Hz = 0.0;
        std::vector<Peak> peaks;
    };

    static bool validIndex(int index) { return index >= 0 && index < kMaxMarkers; }

    std::array<Marker, kMaxMarkers> m_markers{};
    int m_selected = 0;
    bool m_table_visible = false;
};
