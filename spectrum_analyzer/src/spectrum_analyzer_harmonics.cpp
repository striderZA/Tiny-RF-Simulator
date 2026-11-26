#include "spectrum_analyzer_harmonics.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace {

// Median of the finite trace samples: a robust noise-floor estimate when the signal
// occupies only a few bins. An empty or non-finite sample set reads -inf, so no bin can
// clear the peak threshold.
double medianLevel(const std::vector<double> &values) {
    std::vector<double> finite;
    finite.reserve(values.size());
    for (double v : values) {
        if (std::isfinite(v))
            finite.push_back(v);
    }
    if (finite.empty())
        return -std::numeric_limits<double>::infinity();

    const size_t mid = finite.size() / 2;
    std::nth_element(finite.begin(), finite.begin() + static_cast<std::ptrdiff_t>(mid),
                     finite.end());
    double median = finite[mid];
    if (finite.size() % 2 == 0) {
        const double lower =
            *std::max_element(finite.begin(), finite.begin() + static_cast<std::ptrdiff_t>(mid));
        median = 0.5 * (median + lower);
    }
    return median;
}

// Spacing of the first two bins, the width every readout axis uses.
double binWidth(const std::vector<double> &freq_axis) {
    if (freq_axis.size() < 2)
        return 0.0;
    const double step = freq_axis[1] - freq_axis[0];
    return std::isfinite(step) && step > 0.0 ? step : 0.0;
}

// Index of the strongest finite bin within `half_window` of `frequency_Hz` and inside the
// swept span, or -1 when that window holds no bin.
int strongestInWindow(const std::vector<double> &freq_axis, const std::vector<double> &trace_dBm,
                      double frequency_Hz, double half_window, double span_start_Hz,
                      double span_stop_Hz) {
    int best = -1;
    for (size_t i = 0; i < freq_axis.size(); ++i) {
        const double frequency = freq_axis[i];
        if (frequency < span_start_Hz || frequency > span_stop_Hz)
            continue;
        if (std::abs(frequency - frequency_Hz) > half_window)
            continue;
        if (!std::isfinite(trace_dBm[i]))
            continue;
        if (best < 0 || trace_dBm[i] > trace_dBm[static_cast<size_t>(best)])
            best = static_cast<int>(i);
    }
    return best;
}

// Nearest finite bin to `frequency_Hz` inside the swept span, or -1.
int nearestInSpan(const std::vector<double> &freq_axis, const std::vector<double> &trace_dBm,
                  double frequency_Hz, double span_start_Hz, double span_stop_Hz) {
    int best = -1;
    double best_distance = 0.0;
    for (size_t i = 0; i < freq_axis.size(); ++i) {
        const double frequency = freq_axis[i];
        if (!std::isfinite(frequency) || !std::isfinite(trace_dBm[i]))
            continue;
        if (frequency < span_start_Hz || frequency > span_stop_Hz)
            continue;
        const double distance = std::abs(frequency - frequency_Hz);
        if (best < 0 || distance < best_distance) {
            best = static_cast<int>(i);
            best_distance = distance;
        }
    }
    return best;
}

// True when bin `index` is a tone-like peak: a local maximum clear of the noise threshold
// with a dip of kPeakClearance_dB within `dip_bins` bins on each side. A raised noise band
// is full of local maxima but its ripple never dips that far inside the band, so it cannot
// be mistaken for a tone.
bool isClearPeak(const std::vector<double> &trace_dBm, size_t index, double threshold,
                 std::ptrdiff_t dip_bins) {
    const double level = trace_dBm[index];
    if (!std::isfinite(level) || !(level > threshold))
        return false;

    // A two-bin plateau is still a peak (a tone between two bins); a flat run is not.
    const double left = index > 0 ? trace_dBm[index - 1] : -std::numeric_limits<double>::infinity();
    const double right = index + 1 < trace_dBm.size() ? trace_dBm[index + 1]
                                                      : -std::numeric_limits<double>::infinity();
    const bool local_max = (level > left && level >= right) || (level >= left && level > right);
    if (!local_max)
        return false;

    const double dip_target = level - HarmonicModel::kPeakClearance_dB;
    const std::ptrdiff_t n = static_cast<std::ptrdiff_t>(trace_dBm.size());
    const std::ptrdiff_t i = static_cast<std::ptrdiff_t>(index);
    // Past either end of the trace there is nothing more to see.
    const std::ptrdiff_t reach = std::min(dip_bins, n - 1);
    bool left_dip = false;
    bool right_dip = false;
    for (std::ptrdiff_t k = 1; k <= reach; ++k) {
        if (i - k >= 0 && trace_dBm[static_cast<size_t>(i - k)] <= dip_target)
            left_dip = true;
        if (i + k < n && trace_dBm[static_cast<size_t>(i + k)] <= dip_target)
            right_dip = true;
        if (left_dip && right_dip)
            break;
    }
    return left_dip && right_dip;
}

} // namespace

void HarmonicModel::measure(const std::vector<double> &freq_axis,
                            const std::vector<double> &trace_dBm, double span_start_Hz,
                            double span_stop_Hz, double rbw_Hz,
                            std::optional<double> fundamental_Hz) {
    m_result = {};
    for (int order = 2; order <= kMaxOrder; ++order)
        m_result.harmonics[static_cast<size_t>(order - 2)].order = order;

    const double width = binWidth(freq_axis);
    if (freq_axis.size() != trace_dBm.size() || width <= 0.0)
        return;

    const double threshold = medianLevel(trace_dBm) + kPeakClearance_dB;
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
        const int index = nearestInSpan(freq_axis, trace_dBm, f0, span_start_Hz, span_stop_Hz);
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
        const double dip_bins =
            std::max<std::ptrdiff_t>(1, static_cast<std::ptrdiff_t>(std::ceil(rbw / width)));
        int best = -1;
        for (size_t i = 0; i < freq_axis.size(); ++i) {
            const double frequency = freq_axis[i];
            if (!std::isfinite(frequency) || frequency == 0.0 || frequency < span_start_Hz ||
                frequency > span_stop_Hz) {
                continue;
            }
            if (!isClearPeak(trace_dBm, i, threshold, dip_bins))
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

        const int index = strongestInWindow(freq_axis, trace_dBm, entry.expected_Hz, half_window,
                                            span_start_Hz, span_stop_Hz);
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
