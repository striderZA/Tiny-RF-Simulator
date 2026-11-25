#pragma once
#include "spectrum.h"
#include <vector>

// Peak table view state for the Spectrum Analyzer. UI-free: it owns the show/hide flag,
// the strongest local maxima of the current measurement source, and the selected row,
// while the widget owns presentation. The table is display state, never project data.
//
// Rows are a snapshot of a peak search on the same jitter-free readout trace as the Peak
// and marker readouts, so they follow the measurement source, trace visibility, and the
// Max Hold / Min Hold / Video Avg mode. The strongest row anchors every delta at zero.
class PeakTableModel {
  public:
    static constexpr int kMaxPeaks = 5;

    struct Row {
        double frequency_Hz = 0.0;
        double amplitude_dBm = 0.0;
        double delta_frequency_Hz = 0.0; // this peak minus the strongest
        double delta_amplitude_dB = 0.0; // this peak minus the strongest
    };

    bool visible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }

    // Replaces the rows from a peak search: keeps the strongest kMaxPeaks, sorted by
    // descending amplitude. The previous selection index is kept and clamped to the new
    // row count, so a table that stays the same size keeps highlighting the same rank.
    void setPeaks(const std::vector<Peak> &peaks);

    int count() const { return static_cast<int>(m_rows.size()); }
    const std::vector<Row> &rows() const { return m_rows; }

    // Selected row index, or -1 while the table is empty.
    int selected() const { return m_selected; }
    // Clamped into the rows; ignored while the table is empty.
    void select(int index);
    // Moves the selection by `delta` rows, wrapping at either end. Returns true only when
    // the selection changed; an empty table or a single row never moves.
    bool step(int delta);

  private:
    std::vector<Row> m_rows;
    int m_selected = -1;
    bool m_visible = false;
};
