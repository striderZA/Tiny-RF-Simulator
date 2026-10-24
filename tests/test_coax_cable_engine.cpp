#include "amplifier_engine.h"
#include "attenuator_engine.h"
#include "coax_cable_engine.h"
#include "node_graph_engine.h"
#include "signal_generator_engine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

namespace {
// Drive a single tone through the cable; return the output tone power.
double runOneTone(double freq_Hz, double power_dBm, double length_m, int preset_index,
                  double conn_loss_dB = 0.0) {
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

    if (cable.node().outputs[0].tones.empty())
        return -1e9;
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

    REQUIRE(runOneTone(500e6, 0.0, L, MT340) == Approx(-10.73).margin(0.1));
    REQUIRE(runOneTone(2e9, 0.0, L, MT340) == Approx(-21.86).margin(0.1));
    REQUIRE(runOneTone(6e9, 0.0, L, MT340) == Approx(-38.88).margin(0.1));
    REQUIRE(runOneTone(10e9, 0.0, L, MT340) == Approx(-51.10).margin(0.1));
    REQUIRE(runOneTone(18e9, 0.0, L, MT340) == Approx(-70.39).margin(0.1));
}

TEST_CASE("Coax cable noise follows passive thermal model", "[coax][noise]") {
    constexpr double kT = 1.3806e-23 * 290.0;
    constexpr double frequency_Hz = 6e9;
    const double loss_dB = (0.004710 * std::sqrt(6000.0) + 0.000004 * 6000.0) * 100.0;
    const double gain = dbToLinear(-loss_dB);

    NodeGraphEngine graph;
    Spectrum input;
    input.frequencies = {frequency_Hz, frequency_Hz};
    input.noise_W = {kT, kT};
    input.noise_added_W = {0.0, 0.0};
    input.noise_total_W = {kT, kT};

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4);
    cable.setLengthM(100.0);
    cable.node().inputs[0] = &input;
    cable.update(0.0);
    const auto &coax_output = cable.node().outputs[0];

    AttenuatorEngine attenuator(1, graph);
    attenuator.setAttenuation(loss_dB);
    attenuator.node().inputs[0] = &input;
    attenuator.update(0.0);
    const auto &attenuator_output = attenuator.node().outputs[0];

    REQUIRE(coax_output.noise_total_W[0] / kT ==
            Approx(attenuator_output.noise_total_W[0] / kT).epsilon(1e-10));
    REQUIRE(coax_output.noise_total_W[0] / kT == Approx(1.0).epsilon(1e-10));
    REQUIRE(10.0 * std::log10(coax_output.noise_total_W[0] / (gain * kT)) ==
            Approx(loss_dB).margin(1e-6));
    REQUIRE(coax_output.noise_added_W[0] / kT == Approx(1.0 - gain).epsilon(1e-10));

    Spectrum hotter_input = input;
    hotter_input.noise_total_W = {3.0 * kT, 3.0 * kT};

    CoaxCableEngine first_cable(2, graph);
    first_cable.setPresetIndex(4);
    first_cable.setLengthM(100.0);
    first_cable.node().inputs[0] = &hotter_input;
    first_cable.update(0.0);

    AttenuatorEngine second_stage(3, graph);
    second_stage.setAttenuation(3.0);
    second_stage.node().inputs[0] = &first_cable.node().outputs[0];
    second_stage.update(0.0);

    AttenuatorEngine equivalent_stage(4, graph);
    equivalent_stage.setAttenuation(loss_dB + 3.0);
    equivalent_stage.node().inputs[0] = &hotter_input;
    equivalent_stage.update(0.0);
    REQUIRE(second_stage.node().outputs[0].noise_total_W[0] / kT ==
            Approx(equivalent_stage.node().outputs[0].noise_total_W[0] / kT).epsilon(1e-10));
}

TEST_CASE("Coax cable preserves input noise and records added thermal noise", "[coax][noise]") {
    NodeGraphEngine graph;
    Spectrum input;
    input.frequencies = {2e9, 2e9};
    input.noise_total_W = {1e-18, 1e-18};

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4);
    cable.setLengthM(10.0);
    cable.node().inputs[0] = &input;
    cable.update(0.0);

    const auto &out = cable.node().outputs[0];

    const double loss_dB = (0.004710 * std::sqrt(2000.0) + 0.000004 * 2000.0) * 10.0;
    const double gain = dbToLinear(-loss_dB);
    const double kT = 1.3806e-23 * 290.0;
    REQUIRE(out.noise_W[0] / input.noise_total_W[0] == Approx(gain).epsilon(1e-10));
    REQUIRE(out.noise_added_W[0] / kT == Approx(1.0 - gain).epsilon(1e-10));
    REQUIRE(out.noise_total_W[0] / kT ==
            Approx(input.noise_total_W[0] / kT * gain + (1.0 - gain)).epsilon(1e-10));
}

TEST_CASE("Coax cable applies per-tone phase shift", "[coax][phase]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1e9, 0.0, 30.0); // 1 GHz, 0 dBm, 30° initial phase
    gen.update(0.0);

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4); // MT 340, delay 4.76 ns/m
    cable.setLengthM(1.0);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    const auto &out = cable.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    // Expected shift: -360 * (1e9/1e9) * 1 * 4.76 = -1713.6 deg
    REQUIRE(out.tones[0].phase_deg == Approx(30.0 - 1713.6).epsilon(1e-9));
}

TEST_CASE("Coax cable applies per-bin phase shift", "[coax][phase]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);
    // Initialise input per-bin phase to a known constant
    Spectrum *in = const_cast<Spectrum *>(&gen.node().outputs[0]);
    std::fill(in->phase_deg.begin(), in->phase_deg.end(), 0.0);

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4);
    cable.setLengthM(2.0);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    const auto &out = cable.node().outputs[0];
    REQUIRE(out.phase_deg.size() == out.frequencies.size());
    for (size_t i = 0; i < out.frequencies.size(); ++i) {
        const double f_Hz_c = std::clamp(std::abs(out.frequencies[i]), 1.0, 18.5e9);
        const double expected_shift = -360.0 * (f_Hz_c / 1e9) * 2.0 * 4.76;
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

TEST_CASE("Coax cable deserialize clamps out-of-range preset index", "[coax][deserialize]") {
    NodeGraphEngine graph;
    CoaxCableEngine cable(0, graph);

    // Corrupted/hand-edited .rfsim with an out-of-range preset index must be
    // clamped to a valid preset instead of indexing out of bounds.
    cable.deserialize({{"preset_index", 99}, {"length_m", 1.0}, {"connectors_loss_dB", 0.0}});
    REQUIRE(cable.presetIndex() >= 0);
    REQUIRE(static_cast<size_t>(cable.presetIndex()) < kCoaxCablePresets.size());
    REQUIRE(cable.preset().name == kCoaxCablePresets[cable.presetIndex()].name);

    cable.deserialize({{"preset_index", -1}, {"length_m", 1.0}, {"connectors_loss_dB", 0.0}});
    REQUIRE(cable.presetIndex() >= 0);
    REQUIRE(static_cast<size_t>(cable.presetIndex()) < kCoaxCablePresets.size());
    REQUIRE(cable.preset().name == kCoaxCablePresets[cable.presetIndex()].name);

    // Clamped length / connector-loss values must match the setter ranges.
    cable.deserialize({{"preset_index", 4}, {"length_m", 5000.0}, {"connectors_loss_dB", 99.0}});
    REQUIRE(cable.lengthM() == Approx(1000.0));
    REQUIRE(cable.connectorsLossDB() == Approx(30.0));
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

TEST_CASE("Coax cable refuses uncalibrated presets", "[coax][calibration]") {
    // MT 210/230/265/300/480 have no datasheet K1/K2/delay; they must not be selectable.
    NodeGraphEngine graph;
    CoaxCableEngine cable(0, graph);
    const int MT340 = 4;
    REQUIRE(cable.presetIndex() == MT340);
    for (int idx : {0, 1, 2, 3, 5}) {
        cable.setPresetIndex(idx);
        REQUIRE(cable.presetIndex() == MT340);
    }
}

TEST_CASE("Coax cable deserialize falls back from uncalibrated preset", "[coax][calibration]") {
    NodeGraphEngine graph;
    CoaxCableEngine cable(0, graph);
    cable.deserialize({{"preset_index", 0}, {"length_m", 100.0}, {"connectors_loss_dB", 0.0}});
    REQUIRE(cable.presetIndex() == 4);
}

TEST_CASE("Coax cable uncalibrated preset request still attenuates", "[coax][calibration]") {
    // A refused MT 210 request must not leave a lossless wire: output stays MT 340 loss.
    const double out_power = runOneTone(2e9, -10.0, 100.0, 0, 0.0);
    REQUIRE(out_power == Approx(-31.86).margin(0.1));
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
    cable.update(0.0); // no setters, no input change → no recompute
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

TEST_CASE("Coax cable integrates in generator -> cable -> amplifier chain", "[coax][integration]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(2e9, -40.0);
    gen.update(0.0);

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4); // MT 340
    cable.setLengthM(2.0);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    AmplifierEngine amp(0, graph);
    amp.setGain_dB(10.0);
    amp.setNF_dB(0.0); // disable added noise for a clean power check
    amp.node().inputs[0] = &cable.node().outputs[0];
    amp.update(0.0);

    // Cable loss at 2 GHz, 2 m: (0.004710*sqrt(2000) + 0.000004*2000) * 2 = 0.437 dB
    // Chain: -40 - 0.437 + 10 = -30.437 dBm
    REQUIRE(amp.node().outputs[0].tones[0].freq_Hz == 2e9);
    REQUIRE(amp.node().outputs[0].tones[0].power_dBm == Approx(-30.437).margin(0.05));
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
    gen.updateTone(0, 1e9, -25.0); // change input
    gen.update(0.0);
    cable.update(0.0);
    REQUIRE(cable.node().outputs[0].generation != gen_after_first);
}
