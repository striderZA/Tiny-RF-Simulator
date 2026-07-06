#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "equalizer_engine.h"
#include "signal_generator_engine.h"
#include "node_graph_engine.h"
#include <cmath>

using Catch::Approx;

TEST_CASE("Equalizer ideal mode applies flat gain", "[equalizer]") {
    NodeGraphEngine graph;
    EqualizerEngine eq(0, graph);
    eq.setRefGain_dB(10.0);

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    const auto& out = eq.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].power_dBm == Approx(-10.0).margin(0.01));
}

TEST_CASE("Equalizer ideal mode applies slope", "[equalizer]") {
    NodeGraphEngine graph;
    EqualizerEngine eq(0, graph);
    eq.setRefGain_dB(0.0);
    eq.setRefFreq_Hz(100e6);
    eq.setSlope_dBPerDecade(10.0); // +10 dB per decade

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(100e6, -20.0);   // at ref freq: gain = 0 dB
    gen.addTone(1e9, -20.0);     // 1 decade up: gain = +10 dB
    gen.update(0.0);

    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    const auto& out = eq.node().outputs[0];
    REQUIRE(out.tones.size() == 2);

    for (const auto& t : out.tones) {
        if (std::abs(t.freq_Hz - 100e6) < 1.0)
            REQUIRE(t.power_dBm == Approx(-20.0).margin(0.01));
        else if (std::abs(t.freq_Hz - 1e9) < 1.0)
            REQUIRE(t.power_dBm == Approx(-10.0).margin(0.01));
    }
}

TEST_CASE("Equalizer ideal mode applies ref gain + slope combined", "[equalizer]") {
    NodeGraphEngine graph;
    EqualizerEngine eq(0, graph);
    eq.setRefGain_dB(5.0);
    eq.setRefFreq_Hz(50e6);
    eq.setSlope_dBPerDecade(-6.0); // -6 dB/decade (falling)

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(50e6, -30.0);       // ref: gain = +5 dB
    gen.addTone(500e6, -30.0);      // 1 decade up: gain = +5 + (-6) = -1 dB
    gen.update(0.0);

    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    const auto& out = eq.node().outputs[0];
    REQUIRE(out.tones.size() == 2);

    for (const auto& t : out.tones) {
        if (std::abs(t.freq_Hz - 50e6) < 1.0)
            REQUIRE(t.power_dBm == Approx(-25.0).margin(0.01));
        else if (std::abs(t.freq_Hz - 500e6) < 1.0)
            REQUIRE(t.power_dBm == Approx(-31.0).margin(0.5));
    }
}

static std::string s2p_path() {
    return std::string(PROJECT_SOURCE_DIR) +
        "/component_data/amplifiers/adm-3844psm/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
}

TEST_CASE("Equalizer S-param mode applies S21 gain", "[equalizer][sparam]") {
    NodeGraphEngine graph;
    EqualizerEngine eq(0, graph);
    eq.setSParamFilepath(s2p_path());

    REQUIRE(eq.sparamLoaded());
    REQUIRE(eq.sparamMode());

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    const auto& out = eq.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    auto S21 = eq.sparamData().interpolate(1e9, 2);
    double expected = 20.0 * std::log10(std::abs(S21));
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0 + expected).margin(0.5));
}
