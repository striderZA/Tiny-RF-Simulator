#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "s_parameter_amplifier_engine.h"
#include "signal_generator_engine.h"
#include "node_graph_engine.h"
#include <cmath>
#include <numbers>

using Catch::Approx;

TEST_CASE("SParameterAmplifierEngine loads real .s2p and applies frequency-dependent gain",
          "[sparam_amp]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParameterAmplifierEngine spamp(0, graph, path);

    REQUIRE(spamp.loaded());
    REQUIRE(spamp.numPorts() == 2);
    REQUIRE(spamp.freqs().size() > 10);
    REQUIRE(spamp.params().size() == spamp.freqs().size());
    REQUIRE(spamp.params()[0].size() == 4); // 4 S-params for 2-port
    REQUIRE(spamp.forwardParamIdx() == 2); // default S21

    // S21 should have > 10 dB gain at mid-band for this LNA
    int mid = static_cast<int>(spamp.freqs().size()) / 2;
    double s21_mag = std::abs(spamp.params()[mid][2]); // idx 2 = S21
    REQUIRE(s21_mag > 3.0);
}

TEST_CASE("SParameterAmplifierEngine applies phase rotation to tones", "[sparam_amp]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParameterAmplifierEngine spamp(0, graph, path);
    REQUIRE(spamp.loaded());

    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    spamp.node().inputs[0] = gen.node().outputs[0];
    spamp.update(0.0);

    const auto& out = spamp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    // Values from .s2p file line at 1 GHz: S21 = 19.588779 dB, 145.23813 deg
    // -20 dBm input + 19.588779 dB gain = -0.411221 dBm, phase rotated 145.23813 deg
    REQUIRE(out.tones[0].power_dBm == Approx(-0.411221).margin(0.5));
    REQUIRE(out.tones[0].phase_deg == Approx(145.23813).margin(1.0));
}

TEST_CASE("SParameterAmplifierEngine interpolates gain between data points", "[sparam_amp]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParameterAmplifierEngine spamp(0, graph, path);
    REQUIRE(spamp.loaded());

    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1.005e9, -20.0);
    gen.update(0.0);

    spamp.node().inputs[0] = gen.node().outputs[0];
    spamp.update(0.0);

    const auto& out = spamp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0 + 19.585).margin(0.5));
}

TEST_CASE("SParameterAmplifierEngine handles out-of-band frequency", "[sparam_amp]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParameterAmplifierEngine spamp(0, graph, path);
    REQUIRE(spamp.loaded());

    double f_outside = spamp.freqs().back() + 10e9;
    spamp.node().inputs[0].tones = {{f_outside, -20.0, 0.0}};
    spamp.update(0.0);

    REQUIRE(spamp.node().outputs[0].tones.size() == 1);
    // Should clamp to last point — phase should match last S21 phase
    auto S_last = spamp.params().back()[2];
    double expected_phase = std::arg(S_last) * 180.0 / std::numbers::pi;
    REQUIRE(spamp.node().outputs[0].tones[0].phase_deg == Approx(expected_phase).margin(0.1));
}

TEST_CASE("SParameterAmplifierEngine fails gracefully for bad file", "[sparam_amp]") {
    NodeGraphEngine graph;
    SParameterAmplifierEngine spamp(0, graph, "/nonexistent/file.s2p");
    REQUIRE(!spamp.loaded());

    // update() should not crash
    spamp.update(0.0);
}

TEST_CASE("SParameterAmplifierEngine reloads new file at runtime", "[sparam_amp]") {
    NodeGraphEngine graph;
    SParameterAmplifierEngine spamp(0, graph, "/nonexistent/file.s2p");
    REQUIRE(!spamp.loaded());

    std::string valid_path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    spamp.reload(valid_path);
    REQUIRE(spamp.loaded());
    REQUIRE(spamp.numPorts() == 2);
    REQUIRE(spamp.forwardParamIdx() == 2);
}

TEST_CASE("SParameterAmplifierEngine forward param index controls gain selection", "[sparam_amp]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParameterAmplifierEngine spamp(0, graph, path);
    REQUIRE(spamp.loaded());
    REQUIRE(spamp.forwardParamIdx() == 2); // default S21

    // Switching to S11 (idx 0) should produce different gain
    spamp.setForwardParamIdx(0);
    REQUIRE(spamp.forwardParamIdx() == 0);

    double f_mid = (spamp.freqs().front() + spamp.freqs().back()) / 2.0;
    spamp.node().inputs[0].tones = {{f_mid, -20.0, 0.0}};
    spamp.update(0.0);
    REQUIRE(spamp.node().outputs[0].tones.size() == 1);

    // S11 gain should be much lower than S21 (S11 is reflection, ~0 dB)
    REQUIRE(spamp.node().outputs[0].tones[0].power_dBm < -15.0);
}
