#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

// Shared trace helpers for the Spectrum Analyzer's view-only measurements (harmonics and
// IM3). They run on the jitter-free readout trace, so they describe what the displayed
// trace shows: the same bins Peak and the markers read.
namespace spectrum_trace {

// A bin must rise this far above the trace's median level to count as a tone. The trace is
// jitter-free, so this only rejects the noise floor and its rounding ripple.
inline constexpr double kPeakClearance_dB = 6.0;

// Median of the finite trace samples: a robust noise-floor estimate when the signal
// occupies only a few bins. An empty or non-finite sample set reads -inf, so no bin can
// clear the peak threshold.
inline double medianLevel(const std::vector<double> &values) {
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
inline double binWidth(const std::vector<double> &freq_axis) {
    if (freq_axis.size() < 2)
        return 0.0;
    const double step = freq_axis[1] - freq_axis[0];
    return std::isfinite(step) && step > 0.0 ? step : 0.0;
}

// Index of the strongest finite bin within `half_window` of `frequency_Hz` and inside the
// swept span, or -1 when that window holds no bin.
inline int strongestInWindow(const std::vector<double> &freq_axis,
                             const std::vector<double> &trace_dBm, double frequency_Hz,
                             double half_window, double span_start_Hz, double span_stop_Hz) {
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
inline int nearestInSpan(const std::vector<double> &freq_axis, const std::vector<double> &trace_dBm,
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
inline bool isClearPeak(const std::vector<double> &trace_dBm, size_t index, double threshold,
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

    const double dip_target = level - kPeakClearance_dB;
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

// Peak-test reach in bins: one resolution bandwidth, widened to at least one bin so a
// narrow RBW still finds the peak the trace can show.
inline std::ptrdiff_t dipBins(double rbw_Hz, double width_Hz) {
    if (!(width_Hz > 0.0))
        return 1;
    const double rbw = std::isfinite(rbw_Hz) ? std::max(0.0, rbw_Hz) : 0.0;
    return std::max<std::ptrdiff_t>(1, static_cast<std::ptrdiff_t>(std::ceil(rbw / width_Hz)));
}

} // namespace spectrum_trace
