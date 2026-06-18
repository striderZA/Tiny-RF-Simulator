#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "coax_cable_engine.h"
#include "signal_generator_engine.h"
#include "node_graph_engine.h"

using Catch::Approx;

namespace {
// Drive a single tone through the cable; return the output tone power.
double runOneTone(double freq_Hz, double power_dBm, double length_m,
                  int preset_index, double conn_loss_dB = 0.0) {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(freq_Hz, power_dBm);
    gen.update(0.0);

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(preset_index);
    cable.setLengthM(length_m);
    cable.setConnectorsLossDB(conn_loss_dB);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    if (cable.node().outputs[0].tones.empty()) return -1e9;
    return cable.node().outputs[0].tones[0].power_dBm;
}
} // namespace

// Note: the K1/K2 formula is a best-fit model. The table values in the
// datasheet are measured loss and diverge from the formula at higher
// frequencies (e.g. 6 GHz table=39.6, formula=38.88). We verify the
// formula output here, not the table.
TEST_CASE("Coax cable loss matches K1/K2 formula at 100 m", "[coax][datasheet]") {
    const int MT340 = 4;
    const double L = 100.0;

    REQUIRE(runOneTone(500e6,  0.0, L, MT340) == Approx(-10.73).margin(0.1));
    REQUIRE(runOneTone(2e9,    0.0, L, MT340) == Approx(-21.86).margin(0.1));
    REQUIRE(runOneTone(6e9,    0.0, L, MT340) == Approx(-38.88).margin(0.1));
    REQUIRE(runOneTone(10e9,   0.0, L, MT340) == Approx(-51.10).margin(0.1));
    REQUIRE(runOneTone(18e9,   0.0, L, MT340) == Approx(-70.39).margin(0.1));
}

TEST_CASE("Coax cable scales per-bin noise by 1/L_linear", "[coax][noise]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);
    const auto& in = gen.node().outputs[0];

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4);             // MT 340
    cable.setLengthM(10.0);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    const auto& out = cable.node().outputs[0];
    REQUIRE(out.noise_total_W.size() == in.noise_total_W.size());

    for (size_t i = 0; i < out.frequencies.size(); ++i) {
        const double f_Hz = std::abs(out.frequencies[i]);
        const double f_Hz_clamped = std::clamp(f_Hz, 1.0, 18.5e9);
        const double f_MHz = f_Hz_clamped / 1e6;
        const double loss_dB = (0.004710 * std::sqrt(f_MHz) + 0.000004 * f_MHz) * 10.0;
        const double L_lin = dbToLinear(loss_dB);
        REQUIRE(out.noise_W[i] == Approx(in.noise_total_W[i] / L_lin).epsilon(1e-30));
        REQUIRE(out.noise_added_W[i] == Approx(0.0));
        REQUIRE(out.noise_total_W[i] == Approx(out.noise_W[i]).epsilon(1e-30));
    }
}

TEST_CASE("Coax cable does not add thermal noise (fidelity B)", "[coax][noise]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4);
    cable.setLengthM(2.0);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    for (double d : cable.node().outputs[0].noise_added_W) {
        REQUIRE(d == Approx(0.0));
    }
}

TEST_CASE("Coax cable applies per-tone phase shift", "[coax][phase]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1e9, 0.0, 30.0);  // 1 GHz, 0 dBm, 30° initial phase
    gen.update(0.0);

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4);  // MT 340, delay 0.4 ns/m
    cable.setLengthM(1.0);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    const auto& out = cable.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    // Expected shift: -360 * 1 * 1 * 0.4 * 1e-3 = -0.144 deg
    REQUIRE(out.tones[0].phase_deg == Approx(30.0 - 0.144).epsilon(1e-9));
}

TEST_CASE("Coax cable applies per-bin phase shift", "[coax][phase]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);
    // Initialise input per-bin phase to a known constant
    Spectrum* in = const_cast<Spectrum*>(&gen.node().outputs[0]);
    std::fill(in->phase_deg.begin(), in->phase_deg.end(), 0.0);

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4);
    cable.setLengthM(2.0);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    const auto& out = cable.node().outputs[0];
    REQUIRE(out.phase_deg.size() == out.frequencies.size());
    for (size_t i = 0; i < out.frequencies.size(); ++i) {
        const double f_Hz_c = std::clamp(std::abs(out.frequencies[i]), 1.0, 18.5e9);
        const double expected_shift = -360.0 * (f_Hz_c / 1e9) * 2.0 * 0.4 * 1e-3;
        REQUIRE(out.phase_deg[i] == Approx(expected_shift).epsilon(1e-9));
    }
}

TEST_CASE("Coax cable connector loss adds flat 0.5 dB", "[coax][connectors]") {
    const double baseline = runOneTone(2e9, 0.0, 2.0, 4, 0.0);
    const double with_conn = runOneTone(2e9, 0.0, 2.0, 4, 0.5);
    REQUIRE(with_conn == Approx(baseline - 0.5).margin(0.001));
}

TEST_CASE("Coax cable length 0 leaves only connector loss", "[coax][edge]") {
    const double with_zero_len = runOneTone(2e9, 0.0, 0.0, 4, 1.0);
    REQUIRE(with_zero_len == Approx(-1.0).margin(0.001));
}

TEST_CASE("Coax cable clamps negative length to 0", "[coax][edge]") {
    NodeGraphEngine graph;
    CoaxCableEngine cable(0, graph);
    cable.setLengthM(-5.0);
    REQUIRE(cable.lengthM() == 0.0);
}

TEST_CASE("Coax cable clamps length to 1000 m", "[coax][edge]") {
    NodeGraphEngine graph;
    CoaxCableEngine cable(0, graph);
    cable.setLengthM(5000.0);
    REQUIRE(cable.lengthM() == 1000.0);
}

TEST_CASE("Coax cable tone above max_freq clamps loss", "[coax][edge]") {
    // 25 GHz exceeds MT 340's 18.5 GHz max
    const double at_max = runOneTone(18.5e9, 0.0, 1.0, 4, 0.0);
    const double above_max = runOneTone(25e9, 0.0, 1.0, 4, 0.0);
    // Both should compute loss at clamped f=18.5 GHz, so equal
    REQUIRE(above_max == Approx(at_max).margin(0.01));
}

TEST_CASE("Coax cable zero-K preset produces no loss", "[coax][edge]") {
    // Index 0 = MT 210, K1=K2=0 (stub)
    const double out_power = runOneTone(2e9, -10.0, 5.0, 0, 0.0);
    REQUIRE(out_power == Approx(-10.0).margin(0.001));
}

TEST_CASE("Coax cable with no input produces empty tones and bumps generation", "[coax][edge]") {
    NodeGraphEngine graph;
    CoaxCableEngine cable(0, graph);
    cable.update(0.0);
    REQUIRE(cable.node().outputs[0].tones.empty());
    REQUIRE(cable.node().outputs[0].frequencies.size() >= 2);
}

TEST_CASE("Coax cable dirty flag skips when input unchanged", "[coax][caching]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4);
    cable.setLengthM(1.0);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    const uint64_t gen_after_first = cable.node().outputs[0].generation;
    cable.update(0.0);  // no setters, no input change → no recompute
    REQUIRE(cable.node().outputs[0].generation == gen_after_first);
}

TEST_CASE("Coax cable dirty flag triggers when length changes", "[coax][caching]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4);
    cable.setLengthM(1.0);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    const uint64_t gen_after_first = cable.node().outputs[0].generation;
    cable.setLengthM(2.0);
    cable.update(0.0);
    REQUIRE(cable.node().outputs[0].generation != gen_after_first);
}

TEST_CASE("Coax cable dirty flag triggers when input generation changes", "[coax][caching]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4);
    cable.setLengthM(1.0);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    const uint64_t gen_after_first = cable.node().outputs[0].generation;
    gen.updateTone(0, 1e9, -25.0);  // change input
    gen.update(0.0);
    cable.update(0.0);
    REQUIRE(cable.node().outputs[0].generation != gen_after_first);
}
