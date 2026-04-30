#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "s_parameter_amplifier_engine.h"
#include "signal_generator_engine.h"
#include "node_graph_engine.h"
#include <cmath>
#include <numbers>

using Catch::Approx;

TEST_CASE("SParameterAmplifierEngine loads real .s2p and applies frequency-dependent gain", "[sparam_amp]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
                       "/amplifier/data_files/adm-8344psm-s_parameters/ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParameterAmplifierEngine spamp(0, graph, path);

    REQUIRE(spamp.loaded());
    REQUIRE(spamp.filepath() == path);

    // Drive with a tone at 1 GHz where S21 ≈ 19.6 dB (from raw data line 108: 1000000000 -27.116549 -61.78611 19.588779 ...)
    // Wait, S21 is the 3rd and 4th values: 19.588779 dB, 145.23813 deg
    // So gain = 19.59 dB
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    spamp.node().inputs[0] = gen.node().outputs[0];
    spamp.update(0.0);

    const auto& out = spamp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].freq_Hz == 1e9);
    // Expected gain ≈ 19.59 dB, so output power ≈ -20 + 19.59 = -0.41 dBm
    REQUIRE(out.tones[0].power_dBm == Approx(-0.411221).margin(0.5));
}

TEST_CASE("SParameterAmplifierEngine interpolates gain between data points", "[sparam_amp]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
                       "/amplifier/data_files/adm-8344psm-s_parameters/ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParameterAmplifierEngine spamp(0, graph, path);

    SignalGeneratorEngine gen(0, graph);
    // 1.005 GHz is halfway between 1.0 GHz (19.59 dB) and 1.01 GHz (19.58 dB)
    gen.addTone(1.005e9, -20.0);
    gen.update(0.0);

    spamp.node().inputs[0] = gen.node().outputs[0];
    spamp.update(0.0);

    const auto& out = spamp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    // Interpolated gain should be very close to ~19.585 dB
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0 + 19.585).margin(0.5));
}

TEST_CASE("SParameterAmplifierEngine handles out-of-band frequency", "[sparam_amp]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
                       "/amplifier/data_files/adm-8344psm-s_parameters/ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParameterAmplifierEngine spamp(0, graph, path);

    SignalGeneratorEngine gen(0, graph);
    // 1 Hz is below the lowest frequency (10 MHz) — should clamp to edge gain
    gen.addTone(1.0, -20.0);
    gen.update(0.0);

    spamp.node().inputs[0] = gen.node().outputs[0];
    spamp.update(0.0);

    const auto& out = spamp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    // Edge gain at 10 MHz is ~21.2 dB (from first data line)
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0 + 21.2).margin(1.0));
}

TEST_CASE("SParameterAmplifierEngine fails gracefully for bad file", "[sparam_amp]") {
    NodeGraphEngine graph;
    SParameterAmplifierEngine spamp(0, graph, "/nonexistent/file.s2p");
    REQUIRE(!spamp.loaded());

    // update() should still work without crashing (builds default grid when no input)
    spamp.update(0.0);
    REQUIRE(!spamp.node().outputs[0].frequencies.empty());
}
