#pragma once

#include "spectrum.h"
#include "spectrum_analyzer_engine.h"
#include <cmath>
#include <cstddef>
#include <vector>

// Shared fixtures. Each tone entry is one real cosine; a -f entry folds to +f, phase negated.

inline constexpr double kSpsBin_Hz = 10e6;
// 1e-20 W/Hz over a 10 MHz bin => 1e-13 W => -100 dBm noise per bin.
inline constexpr double kSpsNoiseDensity_W_per_Hz = 1e-20;
inline constexpr double kSpsNoiseBin_dBm = -100.0;
inline constexpr double kSpsSymmetricBins = 4001; // -20 to +20 GHz at 10 MHz, bin at 0 Hz

inline Spectrum makeSpsSpectrum(double noise_density, bool complex_baseband) {
    Spectrum spec;
    spec.frequencies.resize(static_cast<size_t>(kSpsSymmetricBins));
    for (size_t i = 0; i < spec.frequencies.size(); ++i)
        spec.frequencies[i] = -20e9 + static_cast<double>(i) * kSpsBin_Hz;
    spec.noise_total_W.assign(spec.frequencies.size(), noise_density);
    spec.is_complex_baseband = complex_baseband;
    spec.generation = 1;
    return spec;
}

// Real-domain grid with no noise, so tone levels are read without a noise floor.
inline Spectrum makeSpsRealToneScene(std::vector<Spectrum::Tone> tones) {
    Spectrum spec = makeSpsSpectrum(0.0, false);
    spec.tones = std::move(tones);
    return spec;
}

inline void configureSpsDeterministic(SpectrumAnalyzerEngine &sa, SpectrumSides sides) {
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(kSpsBin_Hz);
    sa.setVideoBw(kSpsBin_Hz);
    sa.setTraceMode(TraceMode::ClearWrite);
    sa.setSpectrumSides(sides);
}

// Value of the trace on the axis bin nearest to `frequency_Hz`.
inline double spsValueNear(const std::vector<double> &axis, const std::vector<double> &trace,
                           double frequency_Hz) {
    size_t best = 0;
    for (size_t i = 1; i < axis.size(); ++i) {
        if (std::abs(axis[i] - frequency_Hz) < std::abs(axis[best] - frequency_Hz))
            best = i;
    }
    return trace.at(best);
}

// The readout trace and the axis it is sampled on, for the given specs.
struct SpsReadout {
    std::vector<double> axis;
    std::vector<double> dBm;
};

inline SpsReadout spsReadoutOf(const SpectrumAnalyzerEngine &sa,
                               const std::vector<const Spectrum *> &specs) {
    return {sa.combinedReadoutAxis(specs), sa.renderCombinedReadoutSpectrum(specs)};
}
