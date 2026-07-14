#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "attenuator_engine.h"
#include "node_graph_engine.h"
#include "spectrum.h"
#include <cmath>

using Catch::Matchers::WithinAbs;

namespace {

Spectrum buildTestSpectrum() {
    Spectrum s;
    s.frequencies = {1e9, 2e9, 3e9};
    s.tones = {
        {1e9, -10.0, 0.0},
        {2e9, -20.0, 45.0},
        {3e9, -30.0, 90.0}
    };
    s.noise_W.assign(3, 1e-21);
    s.noise_added_W.assign(3, 0.0);
    s.noise_total_W.assign(3, 1e-21);
    s.phase_deg.assign(3, 0.0);
    s.fs_Hz = 1e9;
    return s;
}

} // namespace

TEST_CASE("Attenuator: pass-through at 0 dB", "[attenuator]") {
    NodeGraphEngine graph;
    AttenuatorEngine atten(1, graph);
    atten.setAttenuation(0.0);

    Spectrum input = buildTestSpectrum();
    atten.node().inputs[0] = &input;

    atten.update(0.016);

    const auto& output = atten.node().outputs[0];

    REQUIRE(output.tones.size() == 3);
    REQUIRE(output.tones[0].power_dBm == -10.0);
    REQUIRE(output.tones[1].power_dBm == -20.0);
    REQUIRE(output.tones[2].power_dBm == -30.0);

    REQUIRE_THAT(output.noise_total_W[0], WithinAbs(1e-21, 1e-25));
    REQUIRE_THAT(output.noise_total_W[1], WithinAbs(1e-21, 1e-25));
    REQUIRE_THAT(output.noise_total_W[2], WithinAbs(1e-21, 1e-25));
}

TEST_CASE("Attenuator: flat 6 dB attenuation", "[attenuator]") {
    NodeGraphEngine graph;
    AttenuatorEngine atten(1, graph);
    atten.setAttenuation(6.0);

    Spectrum input = buildTestSpectrum();
    atten.node().inputs[0] = &input;

    atten.update(0.016);

    const auto& output = atten.node().outputs[0];

    REQUIRE_THAT(output.tones[0].power_dBm, WithinAbs(-16.0, 0.01));
    REQUIRE_THAT(output.tones[1].power_dBm, WithinAbs(-26.0, 0.01));
    REQUIRE_THAT(output.tones[2].power_dBm, WithinAbs(-36.0, 0.01));

    double G = std::pow(10.0, -6.0 / 10.0);
    double expected_noise = 1e-21 * G + 1.3806e-23 * 290.0 * (1.0 - G);
    REQUIRE_THAT(output.noise_total_W[0], WithinAbs(expected_noise, 1e-25));
}

TEST_CASE("Attenuator: passive noise model", "[attenuator]") {
    NodeGraphEngine graph;
    AttenuatorEngine atten(1, graph);
    atten.setAttenuation(10.0);

    Spectrum input = buildTestSpectrum();
    atten.node().inputs[0] = &input;

    atten.update(0.016);

    const auto& output = atten.node().outputs[0];

    const double k = 1.3806e-23;
    const double T = 290.0;
    const double G = std::pow(10.0, -10.0 / 10.0);
    const double expected = 1e-21 * G + k * T * (1.0 - G);

    REQUIRE_THAT(output.noise_total_W[0], WithinAbs(expected, 1e-25));
    REQUIRE_THAT(output.noise_total_W[1], WithinAbs(expected, 1e-25));
    REQUIRE_THAT(output.noise_total_W[2], WithinAbs(expected, 1e-25));
}

TEST_CASE("Attenuator: noise floor convergence at high attenuation", "[attenuator]") {
    NodeGraphEngine graph;
    AttenuatorEngine atten(1, graph);
    atten.setAttenuation(100.0);

    Spectrum input = buildTestSpectrum();
    atten.node().inputs[0] = &input;

    atten.update(0.016);

    const auto& output = atten.node().outputs[0];

    const double k = 1.3806e-23;
    const double T = 290.0;
    const double thermal_noise = k * T;

    REQUIRE_THAT(output.noise_total_W[0], WithinAbs(thermal_noise, 1e-22));
}
