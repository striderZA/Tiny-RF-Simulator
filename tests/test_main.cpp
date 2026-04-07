#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "common.h"

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
    // With gain 1 (0 dB), noise figure 0 dB, bin width 1 Hz => added noise = k*T*1
    double added = addedNoisePerBin_W(0.0, 1.0, 1.0);
    double expected = k * T; // 1.3806e-23 * 290 = 4.00374e-21
    REQUIRE(added == Approx(expected).epsilon(0.001));
    
    // With gain 10 (10 dB), noise figure 10 dB, bin width 1e6 Hz
    double added2 = addedNoisePerBin_W(10.0, dbToLinear(10.0), 1e6);
    double expected2 = k * calculateNoiseTemp(10.0) * dbToLinear(10.0) * 1e6;
    REQUIRE(added2 == Approx(expected2).epsilon(0.001));
}