#include "spectrum_analyzer_harmonics.h"
#include "spectrum_analyzer_trace.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace st = spectrum_trace;

void HarmonicModel::measure(const std::vector<double> &freq_axis,
                            const std::vector<double> &trace_dBm, double span_start_Hz,
                            double span_stop_Hz, double rbw_Hz,
                            std::optional<double> fundamental_Hz) {
    m_result = {};
    for (int order = 2; order <= kMaxOrder; ++order)
        m_result.harmonics[static_cast<size_t>(order - 2)].order = order;

    const double width = st::binWidth(freq_axis);
    if (freq_axis.size() != trace_dBm.size() || width <= 0.0)
        return;

    const double threshold = st::medianLevel(trace_dBm) + kPeakClearance_dB;
    // The harmonic search window is one resolution bandwidth wide, widened to at least one
    // bin so a narrow RBW still finds the peak the trace can show.
    const double rbw = std::isfinite(rbw_Hz) ? std::max(0.0, rbw_Hz) : 0.0;
    const double half_window = std::max(0.5 * rbw, width);

    if (fundamental_Hz) {
        const double f0 = *fundamental_Hz;
        if (!std::isfinite(f0) || f0 < span_start_Hz || f0 > span_stop_Hz) {
            m_result.fundamental_status = Status::FundamentalOutOfSpan;
            for (auto &entry : m_result.harmonics)
                entry.status = Status::NoFundamental;
            return;
        }
        const int index = st::nearestInSpan(freq_axis, trace_dBm, f0, span_start_Hz, span_stop_Hz);
        if (index < 0) {
            m_result.fundamental_status = Status::NoFundamental;
            for (auto &entry : m_result.harmonics)
                entry.status = Status::NoFundamental;
            return;
        }
        // The user placed the marker, so its frequency is taken as given, level and all.
        m_result.fundamental_status = Status::Measured;
        m_result.fundamental_Hz = f0;
        m_result.fundamental_dBm = trace_dBm[static_cast<size_t>(index)];
    } else {
        // The strongest tone-like peak in the span. The search is sign-agnostic: a
        // complex-baseband trace is one-sided, so a clear tone can sit at a negative
        // frequency. A real-domain trace mirrors every tone at +-f, so an exact tie
        // prefers the positive image the marker and Peak readouts name.
        const std::ptrdiff_t dip_bins = st::dipBins(rbw, width);
        int best = -1;
        for (size_t i = 0; i < freq_axis.size(); ++i) {
            const double frequency = freq_axis[i];
            if (!std::isfinite(frequency) || frequency == 0.0 || frequency < span_start_Hz ||
                frequency > span_stop_Hz) {
                continue;
            }
            if (!st::isClearPeak(trace_dBm, i, threshold, dip_bins))
                continue;
            if (best < 0) {
                best = static_cast<int>(i);
                continue;
            }
            const double level = trace_dBm[i];
            const double best_level = trace_dBm[static_cast<size_t>(best)];
            const bool stronger = level > best_level + 1e-9;
            const bool positive_tie = level >= best_level - 1e-9 && frequency > 0.0 &&
                                      freq_axis[static_cast<size_t>(best)] < 0.0;
            if (stronger || positive_tie)
                best = static_cast<int>(i);
        }
        if (best < 0) {
            for (auto &entry : m_result.harmonics)
                entry.status = Status::NoFundamental;
            return;
        }
        m_result.fundamental_status = Status::Measured;
        m_result.fundamental_Hz = freq_axis[static_cast<size_t>(best)];
        m_result.fundamental_dBm = trace_dBm[static_cast<size_t>(best)];
    }

    const double f0 = m_result.fundamental_Hz;
    // 0 Hz is dc: it has no harmonic sequence to measure, and a marker left at the axis
    // midpoint must not read as a fundamental.
    if (f0 == 0.0) {
        m_result.fundamental_status = Status::NoFundamental;
        m_result.fundamental_Hz = 0.0;
        m_result.fundamental_dBm = 0.0;
        for (auto &entry : m_result.harmonics) {
            entry.status = Status::NoFundamental;
            entry.expected_Hz = 0.0;
            entry.frequency_Hz = 0.0;
        }
        return;
    }
    // Adjacent harmonic orders are spaced f0 apart; a resolution bandwidth as wide as that
    // spacing blurs them together, so the measurement reports that instead of a peak.
    const bool unresolved = half_window >= 0.5 * std::abs(f0);

    for (auto &entry : m_result.harmonics) {
        entry.expected_Hz = static_cast<double>(entry.order) * f0;
        entry.frequency_Hz = entry.expected_Hz;
        entry.amplitude_dBm = 0.0;
        entry.relative_dBc = 0.0;

        if (entry.expected_Hz < span_start_Hz || entry.expected_Hz > span_stop_Hz) {
            entry.status = Status::OutOfSpan;
            continue;
        }
        if (unresolved) {
            entry.status = Status::Unresolved;
            // The RBW is the limit when half of it reaches half the order spacing; the bin
            // spacing is the limit otherwise.
            entry.rbw_limited = 0.5 * rbw >= 0.5 * std::abs(f0);
            continue;
        }

        const int index = st::strongestInWindow(freq_axis, trace_dBm, entry.expected_Hz,
                                                half_window, span_start_Hz, span_stop_Hz);
        if (index < 0 || !(trace_dBm[static_cast<size_t>(index)] > threshold)) {
            entry.status = Status::NotFound;
            continue;
        }
        entry.status = Status::Measured;
        entry.frequency_Hz = freq_axis[static_cast<size_t>(index)];
        entry.amplitude_dBm = trace_dBm[static_cast<size_t>(index)];
        entry.relative_dBc = entry.amplitude_dBm - m_result.fundamental_dBm;
    }
}
