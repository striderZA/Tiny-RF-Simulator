#include "spectrum_analyzer_peak_table.h"
#include <algorithm>

void PeakTableModel::setPeaks(const std::vector<Peak> &peaks) {
    std::vector<Peak> sorted = peaks;
    std::sort(sorted.begin(), sorted.end(),
              [](const Peak &a, const Peak &b) { return a.power_dBm > b.power_dBm; });
    if (sorted.size() > static_cast<size_t>(kMaxPeaks))
        sorted.resize(static_cast<size_t>(kMaxPeaks));

    m_rows.clear();
    m_rows.reserve(sorted.size());
    if (!sorted.empty()) {
        const Peak &strongest = sorted.front();
        for (const Peak &peak : sorted) {
            m_rows.push_back(Row{peak.freq_Hz, peak.power_dBm, peak.freq_Hz - strongest.freq_Hz,
                                 peak.power_dBm - strongest.power_dBm});
        }
    }

    if (m_rows.empty()) {
        m_selected = -1;
    } else if (m_selected < 0) {
        m_selected = 0;
    } else if (m_selected >= count()) {
        m_selected = count() - 1;
    }
}

void PeakTableModel::select(int index) {
    if (m_rows.empty())
        return;
    if (index < 0)
        m_selected = 0;
    else if (index >= count())
        m_selected = count() - 1;
    else
        m_selected = index;
}

bool PeakTableModel::step(int delta) {
    if (m_rows.empty())
        return false;
    const int count_rows = count();
    const int next = ((m_selected + delta) % count_rows + count_rows) % count_rows;
    if (next == m_selected)
        return false;
    m_selected = next;
    return true;
}
