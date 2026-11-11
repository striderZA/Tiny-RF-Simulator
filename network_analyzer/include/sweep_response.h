#pragma once

#include "spectrum.h"

#include <optional>
#include <vector>

// Sweep helpers shared by the Network Analyzer and the RF budget: one grid, one stimulus, and one
// per-point tone and bin lookup. Each caller applies its own gain and noise rules.

// f_i = std::lerp(start_Hz, stop_Hz, i / (points - 1)); one point gives {start_Hz}; points <= 0
// gives an empty grid.
std::vector<double> linearSweepFrequencies(double start_Hz, double stop_Hz, int points);

// One tone per frequency at power_dBm and phase 0, and a kT thermal bin per frequency with no
// added noise. The stimulus is real, with fs 0.
Spectrum sweepStimulus(const std::vector<double> &frequencies_Hz, double power_dBm);

struct SweepPointResponse {
    std::optional<double> tone_dBm;      // lowest-index tone within 1 Hz of the frequency
    bool tone_out_of_band = false;       // that tone carries a held edge value
    std::optional<double> noise_total_W; // lowest-index bin within 1 Hz of the frequency
    bool noise_out_of_band = false;      // that bin carries a held edge value
};

// One entry per frequency, in order.
std::vector<SweepPointResponse> readSweepResponse(const std::vector<double> &frequencies_Hz,
                                                  const Spectrum &response);

// 10*log10((noise_total_W / dbToLinear(gain_dB)) / (k*T)); NaN when that ratio is not positive.
double sweepNoiseFigure_dB(double noise_total_W, double gain_dB);
