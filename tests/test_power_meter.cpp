#include "power_meter_engine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

namespace {
Spectrum spectrumWithGrid() {
    Spectrum spec;
    spec.frequencies = {0.0, 1e6, 2e6};
    spec.noise_total_W = {0.0, 0.0, 0.0};
    return spec;
}
} // namespace

TEST_CASE("PowerMeter converts one tone from dBm", "[power_meter]") {
    Spectrum spec = spectrumWithGrid();
    spec.tones = {{1e6, 0.0, 0.0}};
    const auto result = PowerMeterEngine{}.measure(&spec);
    REQUIRE(result.valid);
    REQUIRE(result.error == PowerMeterError::None);
    REQUIRE(result.power_dBm == Catch::Approx(0.0));
}

TEST_CASE("PowerMeter sums multiple tones in watts", "[power_meter]") {
    Spectrum spec = spectrumWithGrid();
    spec.tones = {{1e6, 0.0, 0.0}, {2e6, 0.0, 0.0}};
    const auto result = PowerMeterEngine{}.measure(&spec);
    REQUIRE(result.valid);
    REQUIRE(result.power_dBm == Catch::Approx(3.0102999566));
}

TEST_CASE("PowerMeter integrates noise density over all bins", "[power_meter]") {
    Spectrum spec = spectrumWithGrid();
    spec.noise_total_W = {1e-12, 1e-12, 1e-12};
    const auto result = PowerMeterEngine{}.measure(&spec);
    REQUIRE(result.valid);
    REQUIRE(result.power_dBm == Catch::Approx(10.0 * std::log10(3e-6) + 30.0));
}

TEST_CASE("PowerMeter combines tones and integrated noise", "[power_meter]") {
    Spectrum spec = spectrumWithGrid();
    spec.tones = {{1e6, -10.0, 0.0}};
    spec.noise_total_W = {1e-12, 1e-12, 1e-12};
    const double expected_watts = 1e-3 * std::pow(10.0, -10.0 / 10.0) + 3e-6;
    const auto result = PowerMeterEngine{}.measure(&spec);
    REQUIRE(result.valid);
    REQUIRE(result.power_dBm == Catch::Approx(10.0 * std::log10(expected_watts) + 30.0));
}

TEST_CASE("PowerMeter reports valid zero power as negative infinity", "[power_meter]") {
    Spectrum spec = spectrumWithGrid();
    const auto result = PowerMeterEngine{}.measure(&spec);
    REQUIRE(result.valid);
    REQUIRE(result.error == PowerMeterError::None);
    REQUIRE(std::isinf(result.power_dBm));
    REQUIRE(result.power_dBm < 0.0);
}

TEST_CASE("PowerMeter rejects missing and empty spectra", "[power_meter]") {
    PowerMeterEngine engine;
    SECTION("missing source") {
        const auto result = engine.measure(nullptr);
        REQUIRE_FALSE(result.valid);
        REQUIRE(result.error == PowerMeterError::MissingSource);
        REQUIRE(std::isnan(result.power_dBm));
    }
    SECTION("empty frequency grid") {
        Spectrum spec;
        const auto result = engine.measure(&spec);
        REQUIRE_FALSE(result.valid);
        REQUIRE(result.error == PowerMeterError::InvalidFrequencyGrid);
    }
}

TEST_CASE("PowerMeter rejects malformed frequency grids", "[power_meter]") {
    PowerMeterEngine engine;
    SECTION("one frequency") {
        Spectrum spec;
        spec.frequencies = {0.0};
        REQUIRE(engine.measure(&spec).error == PowerMeterError::InvalidFrequencyGrid);
    }
    SECTION("non-increasing") {
        Spectrum spec;
        spec.frequencies = {0.0, 1e6, 1e6};
        REQUIRE(engine.measure(&spec).error == PowerMeterError::InvalidFrequencyGrid);
    }
    SECTION("non-uniform") {
        Spectrum spec;
        spec.frequencies = {0.0, 1e6, 3e6};
        REQUIRE(engine.measure(&spec).error == PowerMeterError::InvalidFrequencyGrid);
    }
    SECTION("non-finite") {
        Spectrum spec;
        spec.frequencies = {0.0, std::numeric_limits<double>::quiet_NaN()};
        REQUIRE(engine.measure(&spec).error == PowerMeterError::InvalidFrequencyGrid);
    }
}

TEST_CASE("PowerMeter rejects malformed noise and tones", "[power_meter]") {
    PowerMeterEngine engine;
    SECTION("noise size mismatch") {
        Spectrum spec = spectrumWithGrid();
        spec.noise_total_W = {1e-12};
        REQUIRE(engine.measure(&spec).error == PowerMeterError::InvalidNoise);
    }
    SECTION("negative noise") {
        Spectrum spec = spectrumWithGrid();
        spec.noise_total_W[1] = -1.0;
        REQUIRE(engine.measure(&spec).error == PowerMeterError::InvalidNoise);
    }
    SECTION("non-finite noise") {
        Spectrum spec = spectrumWithGrid();
        spec.noise_total_W[1] = std::numeric_limits<double>::infinity();
        REQUIRE(engine.measure(&spec).error == PowerMeterError::InvalidNoise);
    }
    SECTION("non-finite tone") {
        Spectrum spec = spectrumWithGrid();
        spec.tones = {{1e6, std::numeric_limits<double>::quiet_NaN(), 0.0}};
        REQUIRE(engine.measure(&spec).error == PowerMeterError::InvalidTone);
    }
}
