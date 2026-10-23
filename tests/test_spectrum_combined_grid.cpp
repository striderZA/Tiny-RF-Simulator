#include "spectrum.h"
#include "spectrum_analyzer_engine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <vector>

// Issue #293: the combined readout must not add traces on different grids by
// index. Every visible trace is sampled onto one common readout axis, so a tone
// reads at its own frequency whatever trace it sits on and whatever order the
// traces are listed in.

namespace {

constexpr double kBinWidth_Hz = 10e6;
constexpr double kToneFreq_Hz = 1.05e9; // tone on the narrow band grid
constexpr double kFarToneFreq_Hz = 5e9; // tone on the full-band grid, outside the band
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

// Full band: -20 to +20 GHz at 10 MHz.
Spectrum makeFullBand() { return makeGrid(-20e9, 4001); }

// Full band with a 0 dBm tone at 5 GHz.
Spectrum makeFullBandWithFarTone() {
    Spectrum spec = makeFullBand();
    spec.tones = {{kFarToneFreq_Hz, kTonePower_dBm, 0.0}};
    return spec;
}

// Narrow band grid 1.00-1.10 GHz at 10 MHz with no tone.
Spectrum makeNarrowBandNoTone() { return makeGrid(1.00e9, 11); }

// Narrow band grid 1.00-1.10 GHz at 10 MHz with a 0 dBm tone at 1.05 GHz.
Spectrum makeNarrowBandWithTone() {
    Spectrum spec = makeNarrowBandNoTone();
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

TEST_CASE("SpectrumAnalyzer: combined peak sits on a narrow-grid tone when the full band is first",
          "[spectrum][combined][issue293]") {
    Spectrum full = makeFullBand();
    Spectrum band = makeNarrowBandWithTone();

    SpectrumAnalyzerEngine sa;
    configureDeterministic(sa);

    std::vector<const Spectrum *> specs = {&full, &band};
    const std::vector<double> axis = sa.combinedReadoutAxis(specs);
    std::vector<double> combined = sa.renderCombinedSpectrum(specs);
    REQUIRE(combined.size() == axis.size());

    const size_t peak = argmax(combined);
    REQUIRE(axis[peak] == Catch::Approx(kToneFreq_Hz));
    REQUIRE(combined[peak] == Catch::Approx(kTonePower_dBm).margin(0.5));
}

TEST_CASE(
    "SpectrumAnalyzer: combined peak sits on a narrow-grid tone when the narrow grid is first",
    "[spectrum][combined][issue293]") {
    Spectrum full = makeFullBand();
    Spectrum band = makeNarrowBandWithTone();

    SpectrumAnalyzerEngine sa;
    configureDeterministic(sa);

    std::vector<const Spectrum *> specs = {&band, &full};
    const std::vector<double> axis = sa.combinedReadoutAxis(specs);
    std::vector<double> combined = sa.renderCombinedSpectrum(specs);
    REQUIRE(combined.size() == axis.size());
    // The axis covers the full band, not just the narrow grid listed first.
    REQUIRE(axis.front() == Catch::Approx(-20e9));
    REQUIRE(axis.back() == Catch::Approx(20e9));

    const size_t peak = argmax(combined);
    REQUIRE(axis[peak] == Catch::Approx(kToneFreq_Hz));
    REQUIRE(combined[peak] == Catch::Approx(kTonePower_dBm).margin(0.5));
}

TEST_CASE("SpectrumAnalyzer: tone outside the first-listed grid keeps its frequency",
          "[spectrum][combined][issue293]") {
    // Narrow grid (no tone) listed first; the 0 dBm tone at 5 GHz lies only on the
    // full-band trace, outside the narrow grid.
    Spectrum narrow = makeNarrowBandNoTone();
    Spectrum full = makeFullBandWithFarTone();

    SpectrumAnalyzerEngine sa;
    configureDeterministic(sa);

    std::vector<const Spectrum *> specs = {&narrow, &full};
    const std::vector<double> axis = sa.combinedReadoutAxis(specs);
    std::vector<double> combined = sa.renderCombinedSpectrum(specs);
    REQUIRE(combined.size() == axis.size());

    const size_t peak = argmax(combined);
    REQUIRE(axis[peak] == Catch::Approx(kFarToneFreq_Hz));
    REQUIRE(combined[peak] == Catch::Approx(kTonePower_dBm).margin(0.5));
}

TEST_CASE("SpectrumAnalyzer: combined readout does not depend on trace order",
          "[spectrum][combined][issue293]") {
    Spectrum full = makeFullBandWithFarTone();
    Spectrum band = makeNarrowBandWithTone();

    SpectrumAnalyzerEngine sa;
    configureDeterministic(sa);

    std::vector<const Spectrum *> forward = {&full, &band};
    std::vector<const Spectrum *> reversed = {&band, &full};

    REQUIRE(sa.combinedReadoutAxis(forward) == sa.combinedReadoutAxis(reversed));
    REQUIRE(sa.renderCombinedSpectrum(forward) == sa.renderCombinedSpectrum(reversed));
}
