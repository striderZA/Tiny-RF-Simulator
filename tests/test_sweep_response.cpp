#include "common.h"
#include "spectrum.h"
#include "sweep_response.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

TEST_CASE("linear sweep grid matches the analyzer's lerp", "[sweep_response]") {
    const auto three = linearSweepFrequencies(1.0e9, 2.0e9, 3);
    REQUIRE(three.size() == 3);
    CHECK(three[0] == 1.0e9);
    CHECK(three[1] == 1.5e9);
    CHECK(three[2] == 2.0e9);
    CHECK(linearSweepFrequencies(1.0e9, 2.0e9, 1) == std::vector<double>{1.0e9});

    const auto grid = linearSweepFrequencies(0.7e9, 2.3e9, 201);
    REQUIRE(grid.size() == 201);
    for (int i = 0; i < 201; ++i)
        CHECK(grid[static_cast<std::size_t>(i)] == std::lerp(0.7e9, 2.3e9, i / 200.0));
}

TEST_CASE("sweep stimulus is one tone and one thermal bin per frequency", "[sweep_response]") {
    const auto stimulus = sweepStimulus({1.0e9, 2.0e9}, -30.0);
    REQUIRE(stimulus.tones.size() == 2);
    CHECK(stimulus.tones[1].freq_Hz == 2.0e9);
    CHECK(stimulus.tones[1].power_dBm == -30.0);
    REQUIRE(stimulus.noise_total_W.size() == 2);
    CHECK(stimulus.noise_total_W[0] == Catch::Approx(k * T));
    CHECK(stimulus.noise_added_W[0] == 0.0);
    CHECK(stimulus.fs_Hz == 0.0);
    CHECK_FALSE(stimulus.is_complex_baseband);
}

TEST_CASE("readout takes the lowest-index tone within 1 Hz and reports flags", "[sweep_response]") {
    Spectrum response;
    response.tones = {
        {1.0e9 + 2.0, -20.0, 0.0}, {1.0e9 - 0.5, -11.0, 0.0}, {1.0e9 + 0.5, -12.0, 0.0}};
    const auto near = readSweepResponse({1.0e9, 5.0e9}, response);
    REQUIRE(near.size() == 2);
    CHECK(near[0].tone_dBm == Catch::Approx(-11.0));
    CHECK_FALSE(near[0].tone_out_of_band);
    CHECK_FALSE(near[1].tone_dBm.has_value());

    Spectrum flagged;
    flagged.tones = {{3.0e9, -12.0, 0.0, true}};
    const auto out = readSweepResponse({3.0e9}, flagged);
    REQUIRE(out.size() == 1);
    CHECK(out[0].tone_dBm == Catch::Approx(-12.0));
    CHECK(out[0].tone_out_of_band);
}

TEST_CASE("readout reports noise bins and their flags", "[sweep_response]") {
    Spectrum response;
    response.frequencies = {1.0e9 - 0.3, 1.0e9 + 0.3};
    response.noise_total_W = {3.0e-20, 4.0e-20};
    response.out_of_band = {false, true};
    const auto near = readSweepResponse({1.0e9}, response);
    REQUIRE(near.size() == 1);
    CHECK(near[0].noise_total_W == Catch::Approx(3.0e-20));
    CHECK_FALSE(near[0].noise_out_of_band);

    Spectrum held;
    held.frequencies = {4.0e9};
    held.noise_total_W = {1.0e-20};
    held.out_of_band = {true};
    const auto flagged = readSweepResponse({4.0e9}, held);
    REQUIRE(flagged.size() == 1);
    CHECK(flagged[0].noise_out_of_band);
}

TEST_CASE("sweep noise figure comes from output noise over gain", "[sweep_response]") {
    CHECK(sweepNoiseFigure_dB(k * T * 10.0, 10.0) == Catch::Approx(0.0).margin(1e-9));
    CHECK(sweepNoiseFigure_dB(k * T * 100.0, 10.0) == Catch::Approx(10.0).margin(1e-9));
    CHECK(std::isnan(sweepNoiseFigure_dB(0.0, 10.0)));
}
