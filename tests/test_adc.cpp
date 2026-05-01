#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "adc_engine.h"
#include "node_graph_engine.h"

using Catch::Approx;

static constexpr double Fs = 1e9;

TEST_CASE("ADC with empty input produces NSD-based noise", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(1, graph);
    adc.setFs_Hz(Fs);
    adc.setNsd_dBm_per_Hz(-155.0);

    Spectrum spec;
    spec.frequencies = {0.0, 1e6, 2e6};
    spec.noise_W = {1e-20, 1e-20, 1e-20};
    adc.node().inputs[0] = spec;
    adc.update(0.0);

    const auto& out = adc.node().outputs[0];
    REQUIRE(out.noise_total_W.size() == 3);
    for (size_t i = 0; i < 3; ++i)
        REQUIRE(out.noise_total_W[i] > 1e-20);
}

TEST_CASE("ADC aliases tones into Nyquist zone", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(2, graph);
    adc.setFs_Hz(Fs);

    // Tone within [0, Fs/2) — not aliased
    {
        Spectrum spec;
        spec.tones = {{100e6, -10.0, 0.0}};
        adc.node().inputs[0] = spec;
        adc.update(0.0);

        const auto& out = adc.node().outputs[0];
        REQUIRE(out.tones.size() == 1);
        REQUIRE(out.tones[0].freq_Hz == Approx(100e6));
    }

    // Tone in upper half of zone: f_RF = 600e6 → alias = Fs - 600e6 = 400e6
    {
        Spectrum spec;
        spec.tones = {{600e6, -10.0, 0.0}};
        adc.node().inputs[0] = spec;
        adc.update(0.0);

        const auto& out = adc.node().outputs[0];
        REQUIRE(out.tones.size() == 1);
        REQUIRE(out.tones[0].freq_Hz == Approx(400e6));
    }

    // Tone in zone 2: f_RF = 1.1e9 → alias = 1.1e9 % 1e9 = 100e6
    {
        Spectrum spec;
        spec.tones = {{1.1e9, 0.0, 0.0}};
        adc.node().inputs[0] = spec;
        adc.update(0.0);

        const auto& out = adc.node().outputs[0];
        REQUIRE(out.tones.size() == 1);
        REQUIRE(out.tones[0].freq_Hz == Approx(100e6));
    }
}

TEST_CASE("ADC preserves tone power and phase", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(3, graph);
    adc.setFs_Hz(Fs);

    Spectrum spec;
    spec.tones = {{100e6, -20.0, 45.0}};
    adc.node().inputs[0] = spec;
    adc.update(0.0);

    const auto& out = adc.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0));
    REQUIRE(out.tones[0].phase_deg == Approx(45.0));
}

TEST_CASE("ADC preserves frequency grid and noise structure", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(4, graph);
    adc.setFs_Hz(Fs);

    Spectrum spec;
    spec.frequencies = {-10e6, 0.0, 10e6};
    spec.noise_W = {1e-18, 1e-18, 1e-18};
    spec.noise_added_W = {0, 0, 0};
    adc.node().inputs[0] = spec;
    adc.update(0.0);

    const auto& out = adc.node().outputs[0];
    // Frequency grid unchanged
    REQUIRE(out.frequencies.size() == 3);
    REQUIRE(out.frequencies[0] == Approx(-10e6));
    REQUIRE(out.frequencies[1] == Approx(0.0));
    REQUIRE(out.frequencies[2] == Approx(10e6));
    // Noise should be increased by NSD
    for (size_t i = 0; i < 3; ++i)
        REQUIRE(out.noise_total_W[i] > 1e-18);
}
