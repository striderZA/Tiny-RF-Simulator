#pragma once

#include <cmath>
#include <vector>

constexpr double MIN_FREQ = -20e9;
constexpr double MAX_FREQ = 20e9;
constexpr double MIN_POWER = -174;
constexpr double MAX_POWER = 10;
// Spectrum Analyzer reference-level ceiling. The shared MAX_POWER stays the cap for
// signal amplitudes and receiver input levels; only the analyzer's Y-axis top uses this.
constexpr double MAX_REF_POWER_DBM = 60;
// A level axis needs its reference top strictly above its minimum so it is neither
// inverted nor blank.
constexpr bool levelAxisValid(double ref_dBm, double min_dBm) { return ref_dBm > min_dBm; }
constexpr double DEFAULT_VBW = 25e6;
constexpr double DEFAULT_RBW = 50e6;
constexpr double k = 1.3806e-23;
constexpr double T = 290.0;
constexpr double R = 50.0;

inline double dbToLinear(double dB) { return std::pow(10.0, dB / 10.0); }

inline double calculateNoiseTemp(double nf_dB) {
    double F = dbToLinear(nf_dB);
    return T * (F - 1.0);
}

inline double addedNoiseDensity_W_per_Hz(double nf_dB, double gain_linear) {
    double Te = calculateNoiseTemp(nf_dB);
    return k * Te * gain_linear;
}

// DEPRECATED: use addedNoiseDensity_W_per_Hz for density (W/Hz) model
inline double addedNoisePerBin_W(double nf_dB, double g, double bin_width) {
    return addedNoiseDensity_W_per_Hz(nf_dB, g) * bin_width;
}

inline void buildDefaultFrequencyGrid(std::vector<double> &freqs) {
    constexpr double start_Hz = MIN_FREQ;
    constexpr double stop_Hz = MAX_FREQ;
    constexpr double f_step_Hz = 10e6;
    size_t n = static_cast<size_t>((stop_Hz - start_Hz) / f_step_Hz);
    if (n < 2)
        n = 2;
    freqs.resize(n);
    for (size_t i = 0; i < n; ++i)
        freqs[i] = start_Hz + i * f_step_Hz;
}
