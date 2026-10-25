// Issue #271: the analyzer's VBW moving average must not reduce a CW tone peak.
// A tone's displayed level is set by the RBW filter; VBW smooths only the noise
// floor. The tone must keep its RBW-filtered level at any VBW, while a wider
// VBW still smooths the noise floor between tones.
#include "spectrum.h"
#include "spectrum_analyzer_engine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numeric>
#include <vector>

namespace {

// Thermal noise density kT at 290 K, W/Hz.
constexpr double kKtDensity_W_per_Hz = 4.0e-21;
constexpr double kGridStep_Hz = 10e6;
constexpr double kToneFreq_Hz = 1e9;
constexpr double kTonePower_dBm = 0.0;
constexpr size_t kBinCount = 21; // 900 MHz .. 1.1 GHz on the 10 MHz grid
constexpr size_t kToneBin = 10;  // 1 GHz

Spectrum makeToneOnKtNoise() {
    Spectrum spec;
    spec.frequencies.resize(kBinCount);
    for (size_t i = 0; i < kBinCount; ++i) {
        spec.frequencies[i] = 900e6 + static_cast<double>(i) * kGridStep_Hz;
    }
    spec.noise_total_W.assign(kBinCount, kKtDensity_W_per_Hz);
    spec.tones = {{kToneFreq_Hz, kTonePower_dBm, 0.0}};
    spec.is_complex_baseband = true;
    spec.generation = 1;
    return spec;
}

double sampleStdDev(const std::vector<double> &v) {
    const double mean = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
    double acc = 0.0;
    for (double x : v) {
        acc += (x - mean) * (x - mean);
    }
    return std::sqrt(acc / static_cast<double>(v.size()));
}

} // namespace

TEST_CASE("VBW leaves a CW tone peak at its RBW-filtered level", "[spectrum][vbw][issue271]") {
    const Spectrum spec = makeToneOnKtNoise();

    SECTION("RBW 1 MHz, VBW 25 MHz") {
        SpectrumAnalyzerEngine sa;
        sa.setNoiseJitterEnabled(false);
        sa.setResBw(1e6);
        sa.setVideoBw(25e6);
        const std::vector<double> trace = sa.renderSpectrum(spec);
        REQUIRE(trace.size() == kBinCount);
        REQUIRE(trace[kToneBin] == Catch::Approx(kTonePower_dBm).margin(0.5));
    }

    SECTION("RBW 10 MHz, VBW 25 MHz") {
        SpectrumAnalyzerEngine sa;
        sa.setNoiseJitterEnabled(false);
        sa.setResBw(10e6);
        sa.setVideoBw(25e6);
        const std::vector<double> trace = sa.renderSpectrum(spec);
        REQUIRE(trace.size() == kBinCount);
        REQUIRE(trace[kToneBin] == Catch::Approx(kTonePower_dBm).margin(0.5));
    }

    SECTION("combined trace, RBW 10 MHz, VBW 25 MHz") {
        SpectrumAnalyzerEngine sa;
        sa.setNoiseJitterEnabled(false);
        sa.setResBw(10e6);
        sa.setVideoBw(25e6);
        const std::vector<const Spectrum *> specs = {&spec};
        const std::vector<double> trace = sa.renderCombinedSpectrum(specs);
        REQUIRE(trace.size() == kBinCount);
        REQUIRE(trace[kToneBin] == Catch::Approx(kTonePower_dBm).margin(0.5));
    }
}

TEST_CASE("VBW still smooths the noise floor between tones", "[spectrum][vbw][issue271]") {
    // Alternating 20 dB noise steps with no tone: a narrow VBW keeps the steps,
    // a wide VBW averages them flat.
    Spectrum spec;
    spec.frequencies.resize(kBinCount);
    for (size_t i = 0; i < kBinCount; ++i) {
        spec.frequencies[i] = 900e6 + static_cast<double>(i) * kGridStep_Hz;
    }
    spec.noise_total_W.resize(kBinCount);
    for (size_t i = 0; i < kBinCount; ++i) {
        spec.noise_total_W[i] = (i % 2 == 0) ? kKtDensity_W_per_Hz : kKtDensity_W_per_Hz * 100.0;
    }
    spec.is_complex_baseband = true;
    spec.generation = 1;

    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(10e6);

    sa.setVideoBw(kGridStep_Hz); // one-bin window: passthrough
    const std::vector<double> narrow = sa.renderSpectrum(spec);
    sa.setVideoBw(100e6); // window spans the grid
    const std::vector<double> wide = sa.renderSpectrum(spec);

    REQUIRE(narrow.size() == kBinCount);
    REQUIRE(wide.size() == kBinCount);
    REQUIRE(sampleStdDev(narrow) > 5.0);
    REQUIRE(sampleStdDev(wide) < sampleStdDev(narrow) / 4.0);
}
