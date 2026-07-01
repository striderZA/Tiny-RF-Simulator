#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "common.h"
#include "equalizer_engine.h"
#include "signal_generator_engine.h"
#include "node_graph_engine.h"

using Catch::Approx;

TEST_CASE("Equalizer identity passes tones unchanged", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.addTone(500e6, -35.0);
    gen.update(0.0);

    EqualizerEngine eq(0, graph);
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    const auto& out = eq.node().outputs[0];
    REQUIRE(out.tones.size() == 2);
    REQUIRE(out.tones[0].freq_Hz == 100e6);
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0));
    REQUIRE(out.tones[1].freq_Hz == 500e6);
    REQUIRE(out.tones[1].power_dBm == Approx(-35.0));
}

TEST_CASE("Equalizer L_DC shifts all tones uniformly", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.addTone(500e6, -30.0);
    gen.update(0.0);

    EqualizerEngine eq(0, graph);
    eq.setLossAtDC(+3.0);                  // +3 dB at DC: removes 3 dB from all tones (positive = attenuation, matches CoaxCableEngine convention)
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    const auto& out = eq.node().outputs[0];
    REQUIRE(out.tones.size() == 2);
    REQUIRE(out.tones[0].power_dBm == Approx(-23.0));
    REQUIRE(out.tones[1].power_dBm == Approx(-33.0));
}

TEST_CASE("Equalizer L_DC dirty flag triggers recompute", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    EqualizerEngine eq(0, graph);
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);
    const uint64_t gen0 = eq.node().outputs[0].generation;

    eq.setLossAtDC(+3.0);
    eq.update(0.0);
    REQUIRE(eq.node().outputs[0].generation != gen0);
    REQUIRE(eq.node().outputs[0].tones[0].power_dBm == Approx(-23.0));
}
