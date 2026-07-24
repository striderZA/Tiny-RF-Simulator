#include "coax_presets.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>

using Catch::Approx;

TEST_CASE("Coax preset table has six MilTech entries", "[coax][presets]") {
    REQUIRE(kCoaxCablePresets.size() == 6);
    REQUIRE(std::string(kCoaxCablePresets[0].name) == "MT 210");
    REQUIRE(std::string(kCoaxCablePresets[1].name) == "MT 230");
    REQUIRE(std::string(kCoaxCablePresets[2].name) == "MT 265");
    REQUIRE(std::string(kCoaxCablePresets[3].name) == "MT 300");
    REQUIRE(std::string(kCoaxCablePresets[4].name) == "MT 340");
    REQUIRE(std::string(kCoaxCablePresets[5].name) == "MT 480");
}

TEST_CASE("MT 340 preset matches datasheet", "[coax][presets]") {
    const CableSpec &mt340 = kCoaxCablePresets[4];
    REQUIRE(mt340.K1_dB_per_m == Approx(0.004710));
    REQUIRE(mt340.K2_dB_per_m == Approx(0.000004));
    REQUIRE(mt340.delay_ns_per_m == Approx(4.76));
    REQUIRE(mt340.max_freq_GHz == Approx(18.5));
    REQUIRE(mt340.diameter_mm == Approx(8.6));
}
