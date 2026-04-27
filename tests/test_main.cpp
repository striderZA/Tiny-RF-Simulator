#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "common.h"
#include "spectrum.h"

using Catch::Approx;

TEST_CASE("dB to linear conversion", "[common]") {
    REQUIRE(dbToLinear(0.0) == Approx(1.0));
    REQUIRE(dbToLinear(10.0) == Approx(10.0));
    REQUIRE(dbToLinear(-10.0) == Approx(0.1));
    REQUIRE(dbToLinear(20.0) == Approx(100.0));
}

TEST_CASE("Noise temperature calculation", "[common]") {
    // Noise figure 0 dB => noise temperature = 0 K
    REQUIRE(calculateNoiseTemp(0.0) == Approx(0.0));
    // Noise figure 3 dB => F = 10^(3/10) ≈ 2.0, Te = T*(F-1) = 290*(2-1)=290 K
    REQUIRE(calculateNoiseTemp(3.0) == Approx(290.0).epsilon(0.01));
}

TEST_CASE("Added noise per bin", "[common]") {
    // With noise figure 0 dB => Te = 0 => added noise = 0 regardless of gain
    double added = addedNoisePerBin_W(0.0, 1.0, 1.0);
    REQUIRE(added == Approx(0.0));
    
    // With gain 10 (10 dB), noise figure 10 dB, bin width 1e6 Hz
    double added2 = addedNoisePerBin_W(10.0, dbToLinear(10.0), 1e6);
    double expected2 = k * calculateNoiseTemp(10.0) * dbToLinear(10.0) * 1e6;
    REQUIRE(added2 == Approx(expected2).epsilon(0.001));
}

TEST_CASE("Spectrum computeTotalNoise", "[common]") {
    Spectrum spec;
    // Set up frequencies (uniform grid)
    const int N = 5;
    spec.frequencies.resize(N);
    for (int i = 0; i < N; ++i) {
        spec.frequencies[i] = i * 1e6; // 1 MHz steps
    }
    spec.noise_W = {1e-12, 2e-12, 3e-12, 4e-12, 5e-12};
    spec.noise_added_W = {0.5e-12, 0.6e-12, 0.7e-12, 0.8e-12, 0.9e-12};
    spec.computeTotalNoise();
    REQUIRE(spec.noise_total_W.size() == N);
    for (int i = 0; i < N; ++i) {
        REQUIRE(spec.noise_total_W[i] == Approx(spec.noise_W[i] + spec.noise_added_W[i]).epsilon(1e-30));
    }
}