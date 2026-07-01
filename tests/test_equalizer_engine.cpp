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

TEST_CASE("Equalizer slope produces 18 dB loss at 1 GHz", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1.0, 0.0);                 // 1 Hz, log10(1) = 0
    gen.addTone(10.0, 0.0);                // 10 Hz, log10(10) = 1
    gen.addTone(1e9, 0.0);                 // 1 GHz, log10(1e9) = 9
    gen.addTone(1e10, 0.0);                // 10 GHz, log10(1e10) = 10
    gen.update(0.0);

    EqualizerEngine eq(0, graph);
    eq.setSlope(2.0);                      // 2 dB/decade
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    const auto& out = eq.node().outputs[0];
    REQUIRE(out.tones[0].power_dBm == Approx(0.0));      // 1 Hz: 0 dB loss
    REQUIRE(out.tones[1].power_dBm == Approx(-2.0));     // 10 Hz: 2 dB loss
    REQUIRE(out.tones[2].power_dBm == Approx(-18.0));    // 1 GHz: 18 dB loss
    REQUIRE(out.tones[3].power_dBm == Approx(-20.0));    // 10 GHz: 20 dB loss
}

TEST_CASE("Equalizer negative slope acts as pre-emphasis", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1e9, 0.0);
    gen.update(0.0);

    EqualizerEngine eq(0, graph);
    eq.setSlope(-2.0);                     // -2 dB/decade
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    REQUIRE(eq.node().outputs[0].tones[0].power_dBm == Approx(18.0));
}

TEST_CASE("Equalizer DC floor clamps f<=0 to L_DC", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);
    // Synthesise a tone at exactly 0 Hz by pushing directly into the output spectrum.
    gen.node().outputs[0].tones.push_back({0.0, 0.0, 0.0});
    gen.update(0.0);

    EqualizerEngine eq(0, graph);
    eq.setLossAtDC(-3.0);
    eq.setSlope(2.0);
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    REQUIRE(eq.node().outputs[0].tones[0].power_dBm == Approx(3.0));   // 0 Hz -> loss = -3 dB
}

TEST_CASE("Equalizer sub-1 Hz floor clamps to L_DC", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);
    gen.node().outputs[0].tones.push_back({0.5, 0.0, 0.0});
    gen.update(0.0);

    EqualizerEngine eq(0, graph);
    eq.setLossAtDC(-3.0);
    eq.setSlope(2.0);
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    REQUIRE(eq.node().outputs[0].tones[0].power_dBm == Approx(3.0));   // 0.5 Hz -> loss = -3 dB
}

TEST_CASE("Equalizer phase passes through unchanged", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);                                    // populate out.tones from m_tones (m_dirty=false afterwards)
    gen.node().outputs[0].tones[0].phase_deg = 42.0;     // set phase on the populated output tone

    EqualizerEngine eq(0, graph);
    eq.setSlope(3.0);                      // ensure slope is non-zero so update() is exercised
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    REQUIRE(eq.node().outputs[0].tones[0].phase_deg == Approx(42.0));
}

TEST_CASE("Equalizer fs_Hz passes through", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);
    gen.node().outputs[0].fs_Hz = 200e6;    // set after update so it sticks

    EqualizerEngine eq(0, graph);
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    REQUIRE(eq.node().outputs[0].fs_Hz == Approx(200e6));
}

TEST_CASE("Equalizer noise density scales by 1/L_linear", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);
    // Manually populate noise on input
    auto& in_spec = gen.node().outputs[0];
    in_spec.noise_total_W.assign(in_spec.frequencies.size(), 1e-20);
    for (auto& v : in_spec.noise_total_W) v = 1e-20;

    EqualizerEngine eq(0, graph);
    eq.setLossAtDC(-3.0);
    eq.node().inputs[0] = &in_spec;
    eq.update(0.0);

    const double L_lin = dbToLinear(-3.0);
    const auto& out = eq.node().outputs[0];
    REQUIRE(out.noise_W.size() == in_spec.frequencies.size());
    for (size_t i = 0; i < out.noise_W.size(); ++i) {
        REQUIRE(out.noise_W[i] == Approx(1e-20 / L_lin).epsilon(1e-9));
        REQUIRE(out.noise_total_W[i] == Approx(out.noise_W[i]).epsilon(1e-30));
        REQUIRE(out.noise_added_W[i] == Approx(0.0).epsilon(1e-30));
    }
}

TEST_CASE("Equalizer with no input produces empty tones on default grid", "[equalizer][edge]") {
    NodeGraphEngine graph;
    EqualizerEngine eq(0, graph);
    eq.update(0.0);

    const auto& out = eq.node().outputs[0];
    REQUIRE(out.tones.empty());
    REQUIRE(out.frequencies.size() >= 2);
    REQUIRE(out.noise_total_W.size() == out.frequencies.size());
    REQUIRE(out.generation > 0);
}
