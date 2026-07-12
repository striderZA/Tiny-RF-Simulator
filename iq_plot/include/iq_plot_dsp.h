#pragma once

#include "spectrum.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>

// Pure DSP helper: builds frequency-domain spectrum from noise + tones,
// then runs IFFT to produce time-domain I/Q samples.
// Extracted from IQPlotWidget for testability.
//
// @param frequencies  Bin centre frequencies (Hz), must be size N >= 2.
// @param noise_total_W  Noise PSD per bin (W/Hz). Missing bins default to 0.
// @param phase_deg  Phase per bin (degrees). Missing bins default to 0.
// @param tones  Tones to inject (freq, power_dBm, phase_deg).
// @param[out] out_i  Time-domain I samples (size N, normalised IFFT output).
// @param[out] out_q  Time-domain Q samples (size N, normalised IFFT output).
// @return false if N < 2 (no output produced).
inline bool build_iq_spectrum(
    const std::vector<double>& frequencies,
    const std::vector<double>& noise_total_W,
    const std::vector<double>& phase_deg,
    const std::vector<Spectrum::Tone>& tones,
    std::vector<std::complex<double>>& out_spectrum)
{
    size_t N = frequencies.size();
    if (N < 2) return false;

    double bin_width = (frequencies.back() - frequencies.front()) / static_cast<double>(N - 1);

    out_spectrum.assign(N, {0.0, 0.0});

    // Noise contribution: magnitude = sqrt(PSD * bin_width) in sqrt(Watts)
    for (size_t i = 0; i < N; ++i) {
        double psd = (i < noise_total_W.size()) ? noise_total_W[i] : 0.0;
        double magnitude = std::sqrt(std::max(0.0, psd * bin_width));
        double phase_rad = (i < phase_deg.size()) ? phase_deg[i] * M_PI / 180.0 : 0.0;
        out_spectrum[i] = std::complex<double>(magnitude * std::cos(phase_rad),
                                                magnitude * std::sin(phase_rad));
    }

    // Tone contribution: magnitude = sqrt(P_mW / 1000) = sqrt(P_W) for scale consistency
    for (const auto& tone : tones) {
        double best_dist = std::numeric_limits<double>::max();
        int best_idx = -1;
        for (size_t i = 0; i < N; ++i) {
            double dist = std::abs(frequencies[i] - tone.freq_Hz);
            if (dist < best_dist) {
                best_dist = dist;
                best_idx = static_cast<int>(i);
            }
        }
        if (best_idx >= 0 && best_idx < static_cast<int>(N)) {
            double mag = std::pow(10.0, tone.power_dBm / 20.0) / std::sqrt(1000.0);
            double phase_rad = tone.phase_deg * M_PI / 180.0;
            out_spectrum[best_idx] += std::complex<double>(mag * std::cos(phase_rad),
                                                            mag * std::sin(phase_rad));
        }
    }

    return true;
}
