#include "sweep_response.h"

#include "common.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace {

// Two frequencies count as the same sweep point within this distance.
constexpr double kFreqEpsilonHz = 1.0;

struct Indexed {
    double freq_Hz;
    std::size_t index;
};

// Entries sorted by frequency, ties broken by index. Non-finite frequencies have no place in the
// order (they break the strict weak order) and match nothing, so they are left out.
std::vector<Indexed> sortedByFrequency(const std::vector<double> &frequencies_Hz) {
    std::vector<Indexed> sorted;
    sorted.reserve(frequencies_Hz.size());
    for (std::size_t i = 0; i < frequencies_Hz.size(); ++i) {
        if (!std::isfinite(frequencies_Hz[i]))
            continue;
        sorted.push_back({frequencies_Hz[i], i});
    }
    std::sort(sorted.begin(), sorted.end(), [](const Indexed &a, const Indexed &b) {
        return a.freq_Hz < b.freq_Hz || (a.freq_Hz == b.freq_Hz && a.index < b.index);
    });
    return sorted;
}

// The lowest index whose frequency lies within kFreqEpsilonHz of f, if any.
std::optional<std::size_t> lowestNear(const std::vector<Indexed> &sorted, double f) {
    // A frequency whose 1 Hz cell lies outside the 64-bit range matches nothing, so such a sweep
    // point reads as no data rather than as a match on a value that cannot be cell-indexed.
    const double cell = std::floor(f / kFreqEpsilonHz);
    constexpr double kMinCell = static_cast<double>(std::numeric_limits<long long>::min());
    constexpr double kMaxCell = static_cast<double>(std::numeric_limits<long long>::max());
    if (!std::isfinite(cell) || cell <= kMinCell || cell >= kMaxCell)
        return std::nullopt;
    auto it =
        std::lower_bound(sorted.begin(), sorted.end(), f - 2.0 * kFreqEpsilonHz,
                         [](const Indexed &entry, double value) { return entry.freq_Hz < value; });
    std::optional<std::size_t> best;
    for (; it != sorted.end() && it->freq_Hz <= f + 2.0 * kFreqEpsilonHz; ++it) {
        if (std::abs(it->freq_Hz - f) <= kFreqEpsilonHz && (!best || it->index < *best))
            best = it->index;
    }
    return best;
}

} // namespace

std::vector<double> linearSweepFrequencies(double start_Hz, double stop_Hz, int points) {
    std::vector<double> frequencies;
    if (points <= 0)
        return frequencies;
    frequencies.reserve(static_cast<std::size_t>(points));
    for (int i = 0; i < points; ++i) {
        const double t = (points > 1) ? static_cast<double>(i) / (points - 1) : 0.0;
        frequencies.push_back(std::lerp(start_Hz, stop_Hz, t));
    }
    return frequencies;
}

Spectrum sweepStimulus(const std::vector<double> &frequencies_Hz, double power_dBm) {
    Spectrum out;
    out.frequencies = frequencies_Hz;
    out.tones.resize(frequencies_Hz.size());
    for (std::size_t i = 0; i < frequencies_Hz.size(); ++i)
        out.tones[i] = {frequencies_Hz[i], power_dBm, 0.0};

    out.noise_W.assign(frequencies_Hz.size(), k * T);
    out.noise_added_W.assign(frequencies_Hz.size(), 0.0);
    out.phase_deg.assign(frequencies_Hz.size(), 0.0);
    out.computeTotalNoise();
    out.fs_Hz = 0.0;
    out.is_complex_baseband = false;
    out.bumpGeneration();
    return out;
}

std::vector<SweepPointResponse> readSweepResponse(const std::vector<double> &frequencies_Hz,
                                                  const Spectrum &response) {
    std::vector<double> tone_frequencies;
    tone_frequencies.reserve(response.tones.size());
    for (const auto &tone : response.tones)
        tone_frequencies.push_back(tone.freq_Hz);
    const auto tones = sortedByFrequency(tone_frequencies);
    const auto bins = sortedByFrequency(response.frequencies);

    std::vector<SweepPointResponse> out(frequencies_Hz.size());
    for (std::size_t i = 0; i < frequencies_Hz.size(); ++i) {
        const double f = frequencies_Hz[i];
        if (const auto tone = lowestNear(tones, f)) {
            out[i].tone_dBm = response.tones[*tone].power_dBm;
            out[i].tone_out_of_band = response.tones[*tone].out_of_band;
        }
        if (const auto bin = lowestNear(bins, f)) {
            if (*bin < response.noise_total_W.size()) {
                out[i].noise_total_W = response.noise_total_W[*bin];
                out[i].noise_out_of_band =
                    *bin < response.out_of_band.size() && response.out_of_band[*bin];
            }
        }
    }
    return out;
}

double sweepNoiseFigure_dB(double noise_total_W, double gain_dB) {
    const double nf_linear = (noise_total_W / dbToLinear(gain_dB)) / (k * T);
    if (!(nf_linear > 0.0))
        return std::numeric_limits<double>::quiet_NaN();
    return 10.0 * std::log10(nf_linear);
}
