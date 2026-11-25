#include "spectrum_analyzer_markers.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

void MarkerModel::select(int index) {
    if (index < 0)
        m_selected = 0;
    else if (index >= kMaxMarkers)
        m_selected = kMaxMarkers - 1;
    else
        m_selected = index;
}

bool MarkerModel::enabled(int index) const {
    return validIndex(index) && m_markers[static_cast<size_t>(index)].enabled;
}

void MarkerModel::setEnabled(int index, bool on) {
    if (validIndex(index))
        m_markers[static_cast<size_t>(index)].enabled = on;
}

bool MarkerModel::delta(int index) const {
    return validIndex(index) && index != kReferenceIndex &&
           m_markers[static_cast<size_t>(index)].delta;
}

void MarkerModel::setDelta(int index, bool on) {
    if (validIndex(index) && index != kReferenceIndex)
        m_markers[static_cast<size_t>(index)].delta = on;
}

double MarkerModel::frequency(int index) const {
    return validIndex(index) ? m_markers[static_cast<size_t>(index)].frequency_Hz : 0.0;
}

void MarkerModel::place(int index, double frequency_Hz) {
    if (!validIndex(index))
        return;
    Marker &marker = m_markers[static_cast<size_t>(index)];
    // A manual move invalidates the cached peak list. Re-placing the same value
    // (the widget does this every frame for the drag line) keeps the cache.
    if (marker.frequency_Hz != frequency_Hz)
        marker.peaks.clear();
    marker.frequency_Hz = frequency_Hz;
}

int MarkerModel::enabledCount() const {
    int count = 0;
    for (const Marker &marker : m_markers)
        if (marker.enabled)
            ++count;
    return count;
}

bool MarkerModel::anyEnabled() const { return enabledCount() > 0; }

int MarkerModel::resolveIndex(int index, const std::vector<double> &freq_axis,
                              const std::vector<double> &data) const {
    if (!enabled(index) || freq_axis.empty() || data.empty())
        return -1;

    const size_t bins = std::min(freq_axis.size(), data.size());
    const double target = m_markers[static_cast<size_t>(index)].frequency_Hz;
    int best_idx = 0;
    double best_dist = std::numeric_limits<double>::max();
    for (size_t i = 0; i < bins; ++i) {
        const double dist = std::abs(freq_axis[i] - target);
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = static_cast<int>(i);
        }
    }
    return best_idx;
}

MarkerModel::Readout MarkerModel::readout(int index, const std::vector<double> &freq_axis,
                                          const std::vector<double> &data) const {
    Readout reading;
    if (!validIndex(index))
        return reading;
    reading.enabled = m_markers[static_cast<size_t>(index)].enabled;
    if (!reading.enabled)
        return reading;

    const int idx = resolveIndex(index, freq_axis, data);
    if (idx < 0)
        return reading;
    reading.valid = true;
    reading.frequency_Hz = freq_axis[static_cast<size_t>(idx)];
    reading.amplitude_dBm = data[static_cast<size_t>(idx)];

    reading.delta = index != kReferenceIndex && m_markers[static_cast<size_t>(index)].delta;
    if (reading.delta && enabled(kReferenceIndex)) {
        const int reference_idx = resolveIndex(kReferenceIndex, freq_axis, data);
        if (reference_idx >= 0) {
            reading.reference_valid = true;
            reading.delta_frequency_Hz =
                reading.frequency_Hz - freq_axis[static_cast<size_t>(reference_idx)];
            reading.delta_amplitude_dB =
                reading.amplitude_dBm - data[static_cast<size_t>(reference_idx)];
        }
    }
    return reading;
}

void MarkerModel::snapToPeak(int index, const std::vector<Peak> &peaks) {
    if (!validIndex(index) || peaks.empty())
        return;
    Marker &marker = m_markers[static_cast<size_t>(index)];
    marker.peaks = peaks;
    marker.frequency_Hz = marker.peaks.front().freq_Hz;
}

bool MarkerModel::nextPeak(int index, const std::vector<Peak> &peaks) {
    if (!validIndex(index))
        return false;
    Marker &marker = m_markers[static_cast<size_t>(index)];
    if (marker.peaks.empty()) {
        if (peaks.empty())
            return false;
        marker.peaks = peaks;
        marker.frequency_Hz = marker.peaks.front().freq_Hz;
        return true;
    }

    int current_rank = 0;
    double best_dist = std::numeric_limits<double>::max();
    for (size_t i = 0; i < marker.peaks.size(); ++i) {
        const double dist = std::abs(marker.peaks[i].freq_Hz - marker.frequency_Hz);
        if (dist < best_dist) {
            best_dist = dist;
            current_rank = static_cast<int>(i);
        }
    }
    const int next_rank = (current_rank + 1) % static_cast<int>(marker.peaks.size());
    marker.frequency_Hz = marker.peaks[static_cast<size_t>(next_rank)].freq_Hz;
    return true;
}

const std::vector<Peak> &MarkerModel::peaks(int index) const {
    static const std::vector<Peak> kEmpty;
    return validIndex(index) ? m_markers[static_cast<size_t>(index)].peaks : kEmpty;
}

std::string MarkerModel::serialize() const {
    char buffer[512];
    std::snprintf(
        buffer, sizeof buffer,
        "rfsim-sa-markers:1|sel:%d|table:%d|m:%d,%d,%.17g|m:%d,%d,%.17g|m:%d,%d,%.17g|"
        "m:%d,%d,%.17g",
        m_selected, m_table_visible ? 1 : 0, m_markers[0].enabled ? 1 : 0,
        m_markers[0].delta ? 1 : 0, m_markers[0].frequency_Hz, m_markers[1].enabled ? 1 : 0,
        m_markers[1].delta ? 1 : 0, m_markers[1].frequency_Hz, m_markers[2].enabled ? 1 : 0,
        m_markers[2].delta ? 1 : 0, m_markers[2].frequency_Hz, m_markers[3].enabled ? 1 : 0,
        m_markers[3].delta ? 1 : 0, m_markers[3].frequency_Hz);
    return buffer;
}

bool MarkerModel::restore(const std::string &token) {
    int selected = 0;
    int table = 0;
    int flags[kMaxMarkers][2] = {};
    double frequencies[kMaxMarkers] = {};

    const int parsed = std::sscanf(
        token.c_str(),
        "rfsim-sa-markers:1|sel:%d|table:%d|m:%d,%d,%lf|m:%d,%d,%lf|m:%d,%d,%lf|m:%d,%d,%lf",
        &selected, &table, &flags[0][0], &flags[0][1], &frequencies[0], &flags[1][0], &flags[1][1],
        &frequencies[1], &flags[2][0], &flags[2][1], &frequencies[2], &flags[3][0], &flags[3][1],
        &frequencies[3]);
    if (parsed != 2 + 2 * kMaxMarkers + kMaxMarkers)
        return false;
    if (selected < 0 || selected >= kMaxMarkers || (table != 0 && table != 1))
        return false;
    for (int i = 0; i < kMaxMarkers; ++i) {
        if ((flags[i][0] != 0 && flags[i][0] != 1) || (flags[i][1] != 0 && flags[i][1] != 1) ||
            !std::isfinite(frequencies[i]))
            return false;
    }

    for (int i = 0; i < kMaxMarkers; ++i) {
        Marker &marker = m_markers[static_cast<size_t>(i)];
        marker.enabled = flags[i][0] != 0;
        marker.delta = i != kReferenceIndex && flags[i][1] != 0;
        marker.frequency_Hz = frequencies[i];
    }
    m_selected = selected;
    m_table_visible = table != 0;
    return true;
}
