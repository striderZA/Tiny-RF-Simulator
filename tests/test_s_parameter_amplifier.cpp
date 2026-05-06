#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "s_parameter_amplifier_engine.h"
#include "s_parameter_filter_engine.h"
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

    spamp.node().inputs[0] = &gen.node().outputs[0];
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

    spamp.node().inputs[0] = &gen.node().outputs[0];
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
    Spectrum in_spec;
    in_spec.tones = {{f_outside, -20.0, 0.0}};
    spamp.node().inputs[0] = &in_spec;
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
    Spectrum in_spec;
    in_spec.tones = {{f_mid, -20.0, 0.0}};
    spamp.node().inputs[0] = &in_spec;
    spamp.update(0.0);
    REQUIRE(spamp.node().outputs[0].tones.size() == 1);

    // S11 gain should be much lower than S21 (S11 is reflection, ~0 dB)
    REQUIRE(spamp.node().outputs[0].tones[0].power_dBm < -15.0);
}

TEST_CASE("SParameterAmplifierEngine NF adds noise power correctly", "[sparam_amp][nf]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParameterAmplifierEngine spamp(0, graph, path);
    REQUIRE(spamp.loaded());

    // No NF — noise_added_W should be zero (or very small)
    spamp.setNF_dB(0.0);
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);
    spamp.node().inputs[0] = &gen.node().outputs[0];
    spamp.update(0.0);

    const auto& out0 = spamp.node().outputs[0];
    REQUIRE(!out0.noise_added_W.empty());
    for (double n : out0.noise_added_W)
        REQUIRE(n == Approx(0.0).margin(1e-30));

    // NF = 3 dB — noise_added_W should be positive
    spamp.setNF_dB(3.0);
    spamp.update(0.0);
    const auto& out3 = spamp.node().outputs[0];
    REQUIRE(!out3.noise_added_W.empty());
    bool any_positive = false;
    for (double n : out3.noise_added_W) {
        if (n > 0.0) { any_positive = true; break; }
    }
    REQUIRE(any_positive);
}

TEST_CASE("SParameterAmplifierEngine nonlinear disabled = no harmonics", "[sparam_amp][nonlinear]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParameterAmplifierEngine spamp(0, graph, path);
    REQUIRE(spamp.loaded());

    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    spamp.setEnableNonlinear(false);
    spamp.node().inputs[0] = &gen.node().outputs[0];
    spamp.update(0.0);

    // Only the fundamental should be present
    const auto& out = spamp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].freq_Hz == 100e6);
}

TEST_CASE("SParameterAmplifierEngine generates harmonics when nonlinear enabled",
          "[sparam_amp][nonlinear]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParameterAmplifierEngine spamp(0, graph, path);
    REQUIRE(spamp.loaded());

    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -40.0);
    gen.update(0.0);

    spamp.setOIP2_dBm(40.0);
    spamp.setOIP3_dBm(30.0);
    spamp.setEnableNonlinear(true);
    spamp.node().inputs[0] = &gen.node().outputs[0];
    spamp.update(0.0);

    const auto& out = spamp.node().outputs[0];
    // Fundamental + 2nd harmonic + 3rd harmonic = 3
    REQUIRE(out.tones.size() >= 3);

    bool found_fund = false, found_h2 = false, found_h3 = false;
    for (const auto& t : out.tones) {
        if (std::abs(t.freq_Hz - 100e6) < 1.0) found_fund = true;
        if (std::abs(t.freq_Hz - 200e6) < 1.0) found_h2 = true;
        if (std::abs(t.freq_Hz - 300e6) < 1.0) found_h3 = true;
    }
    REQUIRE(found_fund);
    REQUIRE(found_h2);
    REQUIRE(found_h3);

    // Harmonics should be weaker than fundamental
    double fund_power = -1e9, h2_power = -1e9;
    for (const auto& t : out.tones) {
        if (std::abs(t.freq_Hz - 100e6) < 1.0) fund_power = t.power_dBm;
        if (std::abs(t.freq_Hz - 200e6) < 1.0) h2_power = t.power_dBm;
    }
    REQUIRE(h2_power < fund_power);
}

TEST_CASE("SParameterAmplifierEngine generates IMD products for two tones",
          "[sparam_amp][nonlinear]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParameterAmplifierEngine spamp(0, graph, path);
    REQUIRE(spamp.loaded());

    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -30.0);
    gen.addTone(101e6, -30.0);
    gen.update(0.0);

    spamp.setOIP2_dBm(40.0);
    spamp.setOIP3_dBm(30.0);
    spamp.setEnableNonlinear(true);
    spamp.node().inputs[0] = &gen.node().outputs[0];
    spamp.update(0.0);

    const auto& out = spamp.node().outputs[0];
    // Should have IMD products including IM3 at 99 MHz and 102 MHz
    REQUIRE(out.tones.size() > 4);

    bool found_im3_lower = false, found_im3_upper = false;
    for (const auto& t : out.tones) {
        if (std::abs(t.freq_Hz - 99e6) < 1.0) found_im3_lower = true;
        if (std::abs(t.freq_Hz - 102e6) < 1.0) found_im3_upper = true;
    }
    REQUIRE(found_im3_lower);
    REQUIRE(found_im3_upper);
}

TEST_CASE("SParameterAmplifierEngine shows compression at high input power",
          "[sparam_amp][nonlinear]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParameterAmplifierEngine spamp(0, graph, path);
    REQUIRE(spamp.loaded());

    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, 0.0);
    gen.update(0.0);

    // Low OIP3 to force compression
    spamp.setOIP2_dBm(30.0);
    spamp.setOIP3_dBm(10.0);
    spamp.setEnableNonlinear(true);
    spamp.node().inputs[0] = &gen.node().outputs[0];
    spamp.update(0.0);

    const auto& out = spamp.node().outputs[0];
    REQUIRE(out.tones.size() >= 1);

    // S21 gain at 100 MHz is ~19.6 dB, so Pout should be ~19.6 dBm linear
    // With OIP3=10 dBm, compression should reduce output below linear expectation
    // Linear Pout = 0 + 19.6 = 19.6 dBm, but compression kicks in
    double actual_gain = out.tones[0].power_dBm - 0.0;
    // Expect compression well below the OIP3 point
    REQUIRE(out.tones[0].power_dBm < 15.0);
}

TEST_CASE("SParameterFilterEngine loads and applies S21 filtering", "[sparam_filter]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParameterFilterEngine spf(0, graph, path);

    REQUIRE(spf.loaded());
    REQUIRE(spf.data().numPorts() == 2);

    // Apply a tone at mid-band
    int mid = static_cast<int>(spf.data().freqs().size()) / 2;
    double f_mid = spf.data().freqs()[mid];
    Spectrum in_spec;
    in_spec.tones = {{f_mid, -20.0, 0.0}};
    spf.node().inputs[0] = &in_spec;
    spf.update(0.0);

    REQUIRE(spf.node().outputs[0].tones.size() == 1);
    // Same gain as S-param amp since both use S21
    REQUIRE(spf.node().outputs[0].tones[0].power_dBm > -15.0);

    // Passive filter: no added noise
    const auto& out = spf.node().outputs[0];
    if (!out.noise_added_W.empty()) {
        for (double n : out.noise_added_W)
            REQUIRE(n == Approx(0.0));
    }
}
