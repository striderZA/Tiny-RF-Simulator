#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "common.h"
#include "spectrum.h"
#include "signal_generator_engine.h"
#include "amplifier_engine.h"
#include "spectrum_analyzer_engine.h"

using Catch::Approx;

TEST_CASE("dB to linear conversion", "[common]") {
    REQUIRE(dbToLinear(0.0) == Approx(1.0));
    REQUIRE(dbToLinear(10.0) == Approx(10.0));
    REQUIRE(dbToLinear(-10.0) == Approx(0.1));
    REQUIRE(dbToLinear(20.0) == Approx(100.0));
}

TEST_CASE("Noise temperature calculation", "[common]") {
    REQUIRE(calculateNoiseTemp(0.0) == Approx(0.0));
    REQUIRE(calculateNoiseTemp(3.0) == Approx(290.0).epsilon(0.01));
}

TEST_CASE("Added noise density", "[common]") {
    double added = addedNoiseDensity_W_per_Hz(0.0, 1.0);
    REQUIRE(added == Approx(0.0));

    double added2 = addedNoiseDensity_W_per_Hz(10.0, dbToLinear(10.0));
    double expected2 = k * calculateNoiseTemp(10.0) * dbToLinear(10.0);
    REQUIRE(added2 == Approx(expected2).epsilon(0.001));
}

TEST_CASE("Added noise per bin (deprecated helper)", "[common]") {
    double density = addedNoiseDensity_W_per_Hz(10.0, dbToLinear(10.0));
    double per_bin = addedNoisePerBin_W(10.0, dbToLinear(10.0), 1e6);
    REQUIRE(per_bin == Approx(density * 1e6).epsilon(0.001));
}

TEST_CASE("Spectrum computeTotalNoise", "[common]") {
    Spectrum spec;
    const int N = 5;
    spec.frequencies.resize(N);
    for (int i = 0; i < N; ++i) {
        spec.frequencies[i] = i * 1e6;
    }
    // Values are now W/Hz (density)
    spec.noise_W = {1e-18, 2e-18, 3e-18, 4e-18, 5e-18};
    spec.noise_added_W = {0.5e-18, 0.6e-18, 0.7e-18, 0.8e-18, 0.9e-18};
    spec.computeTotalNoise();
    REQUIRE(spec.noise_total_W.size() == N);
    for (int i = 0; i < N; ++i) {
        REQUIRE(spec.noise_total_W[i] == Approx(spec.noise_W[i] + spec.noise_added_W[i]).epsilon(1e-30));
    }
}

TEST_CASE("Generator outputs flat thermal noise density", "[generator]") {
    SignalGeneratorEngine gen(0);
    gen.update(0.0);

    const auto &out = gen.node().output;
    REQUIRE(!out.noise_total_W.empty());
    for (double density : out.noise_total_W) {
        REQUIRE(density == Approx(k * T).epsilon(1e-30));
    }
}

TEST_CASE("Amplifier scales noise density correctly", "[amplifier]") {
    SignalGeneratorEngine gen(0);
    gen.update(0.0);

    AmplifierEngine amp(0);
    amp.setGain_dB(10.0);
    amp.setNF_dB(3.0);
    amp.node().input = gen.node().output;
    amp.update(0.0);

    const auto &out = amp.node().output;
    REQUIRE(!out.noise_total_W.empty());

    double G = dbToLinear(10.0);
    double Te = calculateNoiseTemp(3.0);
    double expected_density = k * T * G + k * Te * G;

    for (double density : out.noise_total_W) {
        REQUIRE(density == Approx(expected_density).epsilon(1e-30));
    }
}

TEST_CASE("Spectrum analyzer noise floor depends on RBW not grid spacing", "[spectrum]") {
    SignalGeneratorEngine gen(0);
    gen.update(0.0);

    AmplifierEngine amp(0);
    amp.setGain_dB(20.0);
    amp.setNF_dB(5.0);
    amp.setFreqStep(10e6);
    amp.node().input = gen.node().output;
    amp.update(0.0);

    SpectrumAnalyzerEngine sa;
    sa.setStartFrequency(MIN_FREQ);
    sa.setStopFrequency(MAX_FREQ);
    sa.setResBw(50e6);

    std::vector<const Spectrum *> specs = {&amp.node().output};
    auto display1 = sa.renderCombinedSpectrum(specs);

    // Change amplifier grid spacing; generator has no grid of its own.
    amp.setFreqStep(20e6);
    amp.node().input = gen.node().output;
    amp.update(0.0);

    auto display2 = sa.renderCombinedSpectrum(specs);

    size_t mid = std::min(display1.size(), display2.size()) / 2;
    REQUIRE(display1[mid] == Approx(display2[mid]).epsilon(1.0));

    sa.setResBw(100e6);
    auto display3 = sa.renderCombinedSpectrum(specs);

    REQUIRE(display3[mid] > display2[mid]);
}
