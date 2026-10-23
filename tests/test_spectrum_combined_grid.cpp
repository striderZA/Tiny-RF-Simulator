#include "spectrum.h"
#include "spectrum_analyzer_engine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <vector>

// Issue #293: the combined readout must not add traces on different grids by
// index. The combined trace is resampled onto the first visible trace's grid,
// so its peak has to sit on the tone's frequency wherever the tone's trace is.

namespace {

constexpr double kBinWidth_Hz = 10e6;
constexpr double kToneFreq_Hz = 1.05e9;
constexpr double kTonePower_dBm = 0.0;
// 1e-20 W/Hz over a 10 MHz bin => 1e-13 W => -100 dBm noise floor per bin.
constexpr double kNoiseDensity_W_per_Hz = 1e-20;

Spectrum makeGrid(double start_Hz, size_t bins) {
    Spectrum spec;
    spec.frequencies.resize(bins);
    for (size_t i = 0; i < bins; ++i)
        spec.frequencies[i] = start_Hz + static_cast<double>(i) * kBinWidth_Hz;
    spec.noise_total_W.assign(bins, kNoiseDensity_W_per_Hz);
    spec.is_complex_baseband = true;
    spec.generation = 1;
    return spec;
}

// Full band: -20 to +20 GHz at 10 MHz, no tones.
Spectrum makeFullBand() { return makeGrid(-20e9, 4001); }

// Band grid 1.00-1.10 GHz at 10 MHz, one 0 dBm tone at 1.05 GHz.
Spectrum makeBandWithTone() {
    Spectrum spec = makeGrid(1.00e9, 11);
    spec.tones = {{kToneFreq_Hz, kTonePower_dBm, 0.0}};
    return spec;
}

void configureDeterministic(SpectrumAnalyzerEngine &sa) {
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(kBinWidth_Hz);
    sa.setVideoBw(kBinWidth_Hz);
    sa.setTraceMode(TraceMode::ClearWrite);
}

// Index of the largest value; ties resolve to the lower index.
size_t argmax(const std::vector<double> &v) {
    size_t best = 0;
    for (size_t i = 1; i < v.size(); ++i)
        if (v[i] > v[best])
            best = i;
    return best;
}

} // namespace

TEST_CASE("SpectrumAnalyzer: combined peak sits on the tone across mismatched grids",
          "[spectrum][combined][issue293]") {
    Spectrum full = makeFullBand();
    Spectrum band = makeBandWithTone();

    SpectrumAnalyzerEngine sa;
    configureDeterministic(sa);

    // First visible trace is the full band, so the readout lives on its grid.
    std::vector<const Spectrum *> specs = {&full, &band};
    std::vector<double> combined = sa.renderCombinedSpectrum(specs);
    REQUIRE(combined.size() == full.frequencies.size());

    const size_t peak = argmax(combined);
    REQUIRE(full.frequencies[peak] == Catch::Approx(kToneFreq_Hz));
    REQUIRE(combined[peak] == Catch::Approx(kTonePower_dBm).margin(0.5));
}

TEST_CASE("SpectrumAnalyzer: combined readout follows the first visible grid",
          "[spectrum][combined][issue293]") {
    Spectrum full = makeFullBand();
    Spectrum band = makeBandWithTone();

    SpectrumAnalyzerEngine sa;
    configureDeterministic(sa);

    // Reversed order: the band grid is now the readout grid.
    std::vector<const Spectrum *> specs = {&band, &full};
    std::vector<double> combined = sa.renderCombinedSpectrum(specs);
    REQUIRE(combined.size() == band.frequencies.size());

    const size_t peak = argmax(combined);
    REQUIRE(band.frequencies[peak] == Catch::Approx(kToneFreq_Hz));
    REQUIRE(combined[peak] == Catch::Approx(kTonePower_dBm).margin(0.5));
}
