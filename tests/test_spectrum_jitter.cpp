#include "spectrum.h"
#include "spectrum_analyzer_engine.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numeric>
#include <vector>

// Display noise jitter is a cosmetic "live instrument" effect on the noise
// floor. Deterministic signal tones must not wander with it: the tone peak is
// the same every frame, only the noise between tones fluctuates.

namespace {

constexpr size_t kBinCount = 101;
constexpr int kToneBin = 50;
constexpr double kBinWidth_Hz = 1e6;
constexpr double kTonePower_dBm = -20.0;
// 1e-20 W/Hz over a 1 MHz bin => 1e-14 W => -110 dBm noise floor, ~90 dB below
// the tone, so any jitter leaking into the tone bin is easy to detect.
constexpr double kNoiseDensity_W_per_Hz = 1e-20;

Spectrum makeTonePlusNoise() {
    Spectrum spec;
    spec.frequencies.resize(kBinCount);
    for (size_t i = 0; i < kBinCount; ++i)
        spec.frequencies[i] = static_cast<double>(i) * kBinWidth_Hz;
    spec.noise_total_W.assign(kBinCount, kNoiseDensity_W_per_Hz);
    spec.tones = {{static_cast<double>(kToneBin) * kBinWidth_Hz, kTonePower_dBm, 0.0}};
    // Complex-baseband: a single tone bin, no +-fc conjugate mirror to blur the
    // peak location.
    spec.is_complex_baseband = true;
    spec.generation = 1;
    return spec;
}

void configureJittered(SpectrumAnalyzerEngine &sa) {
    sa.setNoiseJitterEnabled(true);
    sa.setNoiseJitterSigmaDb(1.5);
    sa.setResBw(kBinWidth_Hz);
    sa.setVideoBw(kBinWidth_Hz); // window == 1 bin => VBW passthrough
    sa.setTraceMode(TraceMode::ClearWrite);
}

double sampleStdDev(const std::vector<double> &v) {
    double mean = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
    double acc = 0.0;
    for (double x : v)
        acc += (x - mean) * (x - mean);
    return std::sqrt(acc / static_cast<double>(v.size()));
}

} // namespace

TEST_CASE("SpectrumAnalyzer: jitter moves the noise floor but not a tone peak",
          "[spectrum][jitter]") {
    Spectrum spec = makeTonePlusNoise();

    SpectrumAnalyzerEngine sa;
    configureJittered(sa);

    std::vector<double> tone_bin_samples;
    std::vector<double> noise_bin_samples;
    for (int frame = 0; frame < 200; ++frame) {
        std::vector<double> trace = sa.renderSpectrum(spec);
        REQUIRE(trace.size() == kBinCount);
        tone_bin_samples.push_back(trace[kToneBin]);
        noise_bin_samples.push_back(trace[20]); // far from the tone
    }

    // The noise floor must keep its live fluctuation.
    REQUIRE(sampleStdDev(noise_bin_samples) > 0.5);
    // The deterministic tone must stay put.
    REQUIRE(sampleStdDev(tone_bin_samples) < 0.05);
}

TEST_CASE("SpectrumAnalyzer: combined trace jitter does not move a tone peak",
          "[spectrum][jitter]") {
    Spectrum spec = makeTonePlusNoise();

    SpectrumAnalyzerEngine sa;
    configureJittered(sa);

    std::vector<const Spectrum *> specs = {&spec};
    std::vector<double> tone_bin_samples;
    std::vector<double> noise_bin_samples;
    for (int frame = 0; frame < 200; ++frame) {
        std::vector<double> trace = sa.renderCombinedSpectrum(specs);
        REQUIRE(trace.size() == kBinCount);
        tone_bin_samples.push_back(trace[kToneBin]);
        noise_bin_samples.push_back(trace[20]);
    }

    REQUIRE(sampleStdDev(noise_bin_samples) > 0.5);
    REQUIRE(sampleStdDev(tone_bin_samples) < 0.05);
}

TEST_CASE("SpectrumAnalyzer: jitter leaves a tone-only spectrum bit-identical",
          "[spectrum][jitter]") {
    // No noise floor to randomize: jitter must be a no-op, so two frames match
    // exactly on both render paths instead of nudging the tone.
    Spectrum spec = makeTonePlusNoise();
    spec.noise_total_W.assign(kBinCount, 0.0);

    SpectrumAnalyzerEngine sa;
    configureJittered(sa);

    std::vector<double> first = sa.renderSpectrum(spec);
    std::vector<double> second = sa.renderSpectrum(spec);
    std::vector<const Spectrum *> specs = {&spec};
    std::vector<double> combined_first = sa.renderCombinedSpectrum(specs);
    std::vector<double> combined_second = sa.renderCombinedSpectrum(specs);

    REQUIRE(first.size() == second.size());
    REQUIRE(combined_first.size() == combined_second.size());
    for (size_t i = 0; i < first.size(); ++i) {
        REQUIRE(first[i] == second[i]);
        REQUIRE(combined_first[i] == combined_second[i]);
    }
}

TEST_CASE("SpectrumAnalyzer: tone peak is unchanged when jitter is disabled",
          "[spectrum][jitter]") {
    Spectrum spec = makeTonePlusNoise();

    SpectrumAnalyzerEngine sa;
    configureJittered(sa);
    sa.setNoiseJitterEnabled(false);

    for (int frame = 0; frame < 10; ++frame) {
        std::vector<double> trace = sa.renderSpectrum(spec);
        REQUIRE(trace[kToneBin] > kTonePower_dBm - 0.5);
        REQUIRE(trace[kToneBin] < kTonePower_dBm + 0.5);
    }
}

TEST_CASE("SpectrumAnalyzer: non-positive jitter sigma is a safe no-op", "[spectrum][jitter]") {
    // std::normal_distribution requires a positive sigma; an invalid setting
    // must not be constructed and must leave rendering deterministic.
    Spectrum spec = makeTonePlusNoise();

    SpectrumAnalyzerEngine sa;
    configureJittered(sa);
    sa.setNoiseJitterSigmaDb(-1.0);

    std::vector<double> first = sa.renderSpectrum(spec);
    std::vector<double> second = sa.renderSpectrum(spec);
    std::vector<const Spectrum *> specs = {&spec};
    std::vector<double> combined = sa.renderCombinedSpectrum(specs);

    REQUIRE(first.size() == second.size());
    REQUIRE(combined.size() == first.size());
    for (size_t i = 0; i < first.size(); ++i)
        REQUIRE(first[i] == second[i]);
}
