
#include "spectrum.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Thermal noise power is non-negative", "[spectrum]") {
    Spectrum s;

    double bin_width = 1e6;
    double p = s.thermalNoisePower_W(bin_width);

    REQUIRE(p >= 0.0);
}

TEST_CASE("Total noise is computed per bin", "[spectrum]") {
    Spectrum s;
    s.frequencies = {0, 1e6, 2e6};
    s.noise_W = {1e-12, 2e-12, 3e-12};
    s.noise_added_W = {1e-13, 1e-13, 1e-13};

    s.computeTotalNoise();

    REQUIRE(s.noise_total_W.size() == 3);
    REQUIRE(s.noise_total_W[0] > s.noise_W[0]);
}
