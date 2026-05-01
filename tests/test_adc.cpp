#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "adc_engine.h"
#include "node_graph_engine.h"

using Catch::Approx;

static constexpr double Fs = 1e9;
static constexpr double FC = 2.4e9;

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

TEST_CASE("ADC tone at f_channel shifts to DC", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(2, graph);
    adc.setFs_Hz(Fs);
    adc.setFChannel_Hz(FC);

    Spectrum spec;
    spec.tones = {{FC, 0.0, 0.0}};
    adc.node().inputs[0] = spec;
    adc.update(0.0);

    const auto& out = adc.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].freq_Hz == Approx(0.0).margin(1.0));
    REQUIRE(out.tones[0].power_dBm == Approx(0.0));
}

TEST_CASE("ADC aliases tones into Nyquist zone", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(3, graph);
    adc.setFs_Hz(Fs);
    adc.setFChannel_Hz(FC);

    // Tone at f_channel + 10 MHz → alias then shift → 10e6
    {
        Spectrum spec;
        spec.tones = {{FC + 10e6, -10.0, 0.0}};
        adc.node().inputs[0] = spec;
        adc.update(0.0);

        const auto& out = adc.node().outputs[0];
        REQUIRE(out.tones.size() == 1);
        REQUIRE(out.tones[0].freq_Hz == Approx(10e6).margin(1e3));
    }

    // Even Nyquist zone (zone 4): f_RF = 1.6e9, should alias to DC
    {
        double f_even = 1.6e9;
        AdcEngine adc2(4, graph);
        adc2.setFs_Hz(Fs);
        adc2.setFChannel_Hz(f_even);

        Spectrum spec;
        spec.tones = {{f_even, 0.0, 0.0}};
        adc2.node().inputs[0] = spec;
        adc2.update(0.0);

        const auto& out = adc2.node().outputs[0];
        REQUIRE(out.tones.size() == 1);
        REQUIRE(out.tones[0].freq_Hz == Approx(0.0).margin(1.0));
    }
}

TEST_CASE("ADC frequency grid is shifted by f_channel", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(5, graph);
    adc.setFs_Hz(Fs);
    adc.setFChannel_Hz(FC);

    Spectrum spec;
    spec.frequencies = {FC - 10e6, FC, FC + 10e6};
    spec.noise_W = {1e-20, 1e-20, 1e-20};
    spec.noise_added_W = {0, 0, 0};
    adc.node().inputs[0] = spec;
    adc.update(0.0);

    const auto& out = adc.node().outputs[0];
    REQUIRE(out.frequencies.size() == 3);
    double f_dig_chan = 400e6;
    REQUIRE(out.frequencies[0] == Approx(FC - 10e6 - f_dig_chan).margin(1.0));
    REQUIRE(out.frequencies[1] == Approx(FC - f_dig_chan).margin(1.0));
    REQUIRE(out.frequencies[2] == Approx(FC + 10e6 - f_dig_chan).margin(1.0));
}

TEST_CASE("ADC preserves tone power and phase", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(6, graph);
    adc.setFs_Hz(Fs);
    adc.setFChannel_Hz(FC);

    Spectrum spec;
    spec.tones = {{FC, -20.0, 45.0}};
    adc.node().inputs[0] = spec;
    adc.update(0.0);

    const auto& out = adc.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0));
    REQUIRE(out.tones[0].phase_deg == Approx(45.0));
}
