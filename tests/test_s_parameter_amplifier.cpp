#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "s_param_engine.h"
#include "signal_generator_engine.h"
#include "node_graph_engine.h"
#include <cmath>
#include <numbers>

using Catch::Approx;

TEST_CASE("SParamEngine loads real .s2p and applies frequency-dependent gain",
          "[sparam]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);

    REQUIRE(sp.loaded());
    REQUIRE(sp.data().numPorts() == 2);
    REQUIRE(sp.data().freqs().size() > 10);
    REQUIRE(sp.data().params().size() == sp.data().freqs().size());
    REQUIRE(sp.data().params()[0].size() == 4); // 4 S-params for 2-port
    REQUIRE(sp.forwardParamIdx() == 2); // default S21

    // S21 should have > 10 dB gain at mid-band
    int mid = static_cast<int>(sp.data().freqs().size()) / 2;
    double s21_mag = std::abs(sp.data().params()[mid][2]); // idx 2 = S21
    REQUIRE(s21_mag > 3.0);
}

TEST_CASE("SParamEngine applies phase rotation to tones", "[sparam]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    sp.node().inputs[0] = &gen.node().outputs[0];
    sp.update(0.0);

    const auto& out = sp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].power_dBm == Approx(-0.411221).margin(0.5));
    REQUIRE(out.tones[0].phase_deg == Approx(145.23813).margin(1.0));
}

TEST_CASE("SParamEngine interpolates gain between data points", "[sparam]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1.005e9, -20.0);
    gen.update(0.0);

    sp.node().inputs[0] = &gen.node().outputs[0];
    sp.update(0.0);

    const auto& out = sp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0 + 19.585).margin(0.5));
}

TEST_CASE("SParamEngine handles out-of-band frequency", "[sparam]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    double f_outside = sp.data().freqs().back() + 10e9;
    Spectrum in_spec;
    in_spec.tones = {{f_outside, -20.0, 0.0}};
    sp.node().inputs[0] = &in_spec;
    sp.update(0.0);

    REQUIRE(sp.node().outputs[0].tones.size() == 1);
    auto S_last = sp.data().params().back()[2];
    double expected_phase = std::arg(S_last) * 180.0 / std::numbers::pi;
    REQUIRE(sp.node().outputs[0].tones[0].phase_deg == Approx(expected_phase).margin(0.1));
}

TEST_CASE("SParamEngine fails gracefully for bad file", "[sparam]") {
    NodeGraphEngine graph;
    SParamEngine sp(0, graph, "/nonexistent/file.s2p");
    REQUIRE(!sp.loaded());
    sp.update(0.0); // should not crash
}

TEST_CASE("SParamEngine reloads new file at runtime", "[sparam]") {
    NodeGraphEngine graph;
    SParamEngine sp(0, graph, "/nonexistent/file.s2p");
    REQUIRE(!sp.loaded());

    std::string valid_path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    sp.reload(valid_path);
    REQUIRE(sp.loaded());
    REQUIRE(sp.data().numPorts() == 2);
    REQUIRE(sp.forwardParamIdx() == 2);
}

TEST_CASE("SParamEngine forward param index controls gain selection", "[sparam]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());
    REQUIRE(sp.forwardParamIdx() == 2);

    sp.setForwardParamIdx(0);
    REQUIRE(sp.forwardParamIdx() == 0);

    double f_mid = (sp.data().freqs().front() + sp.data().freqs().back()) / 2.0;
    Spectrum in_spec;
    in_spec.tones = {{f_mid, -20.0, 0.0}};
    sp.node().inputs[0] = &in_spec;
    sp.update(0.0);
    REQUIRE(sp.node().outputs[0].tones.size() == 1);
    REQUIRE(sp.node().outputs[0].tones[0].power_dBm < -15.0);
}

TEST_CASE("SParamEngine NF adds noise power correctly", "[sparam][nf]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    // Default NF=0 — noise_added_W should be zero (passive behavior)
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);
    sp.node().inputs[0] = &gen.node().outputs[0];
    sp.update(0.0);

    const auto& out0 = sp.node().outputs[0];
    REQUIRE(!out0.noise_added_W.empty());
    for (double n : out0.noise_added_W)
        REQUIRE(n == Approx(0.0).margin(1e-30));

    // NF = 3 dB — noise_added_W should be positive
    sp.setNF_dB(3.0);
    sp.update(0.0);
    const auto& out3 = sp.node().outputs[0];
    REQUIRE(!out3.noise_added_W.empty());
    bool any_positive = false;
    for (double n : out3.noise_added_W) {
        if (n > 0.0) { any_positive = true; break; }
    }
    REQUIRE(any_positive);
}

TEST_CASE("SParamEngine nonlinear disabled = no harmonics", "[sparam][nonlinear]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    sp.setEnableNonlinear(false);
    sp.node().inputs[0] = &gen.node().outputs[0];
    sp.update(0.0);

    const auto& out = sp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].freq_Hz == 100e6);
}

TEST_CASE("SParamEngine generates harmonics when nonlinear enabled",
          "[sparam][nonlinear]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -40.0);
    gen.update(0.0);

    sp.setOIP2_dBm(40.0);
    sp.setOIP3_dBm(30.0);
    sp.setEnableNonlinear(true);
    sp.node().inputs[0] = &gen.node().outputs[0];
    sp.update(0.0);

    const auto& out = sp.node().outputs[0];
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
}

TEST_CASE("SParamEngine generates IMD products for two tones",
          "[sparam][nonlinear]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -30.0);
    gen.addTone(101e6, -30.0);
    gen.update(0.0);

    sp.setOIP2_dBm(40.0);
    sp.setOIP3_dBm(30.0);
    sp.setEnableNonlinear(true);
    sp.node().inputs[0] = &gen.node().outputs[0];
    sp.update(0.0);

    const auto& out = sp.node().outputs[0];
    REQUIRE(out.tones.size() > 4);

    bool found_im3_lower = false, found_im3_upper = false;
    for (const auto& t : out.tones) {
        if (std::abs(t.freq_Hz - 99e6) < 1.0) found_im3_lower = true;
        if (std::abs(t.freq_Hz - 102e6) < 1.0) found_im3_upper = true;
    }
    REQUIRE(found_im3_lower);
    REQUIRE(found_im3_upper);
}

TEST_CASE("SParamEngine shows compression at high input power",
          "[sparam][nonlinear]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, 0.0);
    gen.update(0.0);

    sp.setOIP2_dBm(30.0);
    sp.setOIP3_dBm(10.0);
    sp.setEnableNonlinear(true);
    sp.node().inputs[0] = &gen.node().outputs[0];
    sp.update(0.0);

    const auto& out = sp.node().outputs[0];
    REQUIRE(out.tones.size() >= 1);
    REQUIRE(out.tones[0].power_dBm < 15.0);
}

TEST_CASE("SParamEngine default is passive (no NF, no nonlinear)", "[sparam]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    REQUIRE(sp.nf_dB() == Approx(0.0));
    REQUIRE(!sp.enableNonlinear());

    int mid = static_cast<int>(sp.data().freqs().size()) / 2;
    double f_mid = sp.data().freqs()[mid];
    Spectrum in_spec;
    in_spec.tones = {{f_mid, -20.0, 0.0}};
    sp.node().inputs[0] = &in_spec;
    sp.update(0.0);

    const auto& out = sp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    if (!out.noise_added_W.empty()) {
        for (double n : out.noise_added_W)
            REQUIRE(n == Approx(0.0));
    }
}

TEST_CASE("SParamEngine loads 3-port .s3p file correctly", "[sparam][multiport]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p";
    SParamEngine sp(0, graph, path);

    REQUIRE(sp.loaded());
    REQUIRE(sp.data().numPorts() == 3);
    REQUIRE(sp.numPorts() == 3);
    REQUIRE(sp.data().params()[0].size() == 9); // 3x3 = 9 S-params

    // Default mode is full matrix
    REQUIRE(sp.fullMatrixMode());
}

TEST_CASE("SParamEngine 3-port splitter: single input produces two outputs",
          "[sparam][multiport]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());
    REQUIRE(sp.numPorts() == 3);

    // Wire generator to Port 2 input (COMMON port on this splitter)
    Spectrum in_spec;
    in_spec.tones = {{2e9, -10.0, 0.0}};  // 2 GHz, -10 dBm, 0° phase
    in_spec.frequencies = {2e9};
    in_spec.noise_W = {1e-12};
    in_spec.noise_added_W = {0.0};
    in_spec.noise_total_W = {1e-12};

    // Wire only Port 2
    sp.node().inputs[0] = nullptr;  // Port 1: Z₀
    sp.node().inputs[1] = &in_spec; // Port 2: generator
    sp.node().inputs[2] = nullptr;  // Port 3: Z₀

    sp.update(0.0);

    // Port 1 output should have a tone (from S₁₂ × input₂)
    const auto& out1 = sp.node().outputs[0];
    REQUIRE(out1.tones.size() == 1);
    REQUIRE(out1.tones[0].freq_Hz == Approx(2e9));

    // Port 2 output should also have a tone (from S₂₂ × input₂)
    const auto& out2 = sp.node().outputs[1];
    REQUIRE(out2.tones.size() >= 1);

    // Port 3 output should have a tone (from S₃₂ × input₂)
    const auto& out3 = sp.node().outputs[2];
    REQUIRE(out3.tones.size() == 1);
    REQUIRE(out3.tones[0].freq_Hz == Approx(2e9));

    // For a 3-way splitter, all outputs should be at roughly the same level
    // (within a few dB — the datasheet shows ~-3.6 dB coupling)
    REQUIRE(out1.tones[0].power_dBm == Approx(-10.0 - 3.63).margin(0.5));
    REQUIRE(out3.tones[0].power_dBm == Approx(-10.0 - 3.95).margin(0.5));
}

TEST_CASE("SParamEngine 3-port combiner: two inputs sum at common port",
          "[sparam][multiport]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    // Wire two generators at different frequencies into Port 1 and Port 3
    Spectrum in_a, in_b;
    in_a.tones = {{2e9, -10.0, 0.0}};
    in_a.frequencies = {2e9};
    in_a.noise_W = {1e-12};
    in_a.noise_added_W = {0.0};
    in_a.noise_total_W = {1e-12};

    in_b.tones = {{2.001e9, -10.0, 0.0}};
    in_b.frequencies = {2.001e9};
    in_b.noise_W = {1e-12};
    in_b.noise_added_W = {0.0};
    in_b.noise_total_W = {1e-12};

    sp.node().inputs[0] = &in_a; // Port 1: gen A
    sp.node().inputs[1] = nullptr; // Port 2: Z₀
    sp.node().inputs[2] = &in_b; // Port 3: gen B

    sp.update(0.0);

    // Port 2 output should have both tones (S₂₁ × input₁ + S₂₃ × input₃)
    const auto& out = sp.node().outputs[1]; // Port 2 output
    REQUIRE(out.tones.size() >= 2);

    bool found_2g = false, found_2001 = false;
    for (const auto& t : out.tones) {
        if (std::abs(t.freq_Hz - 2e9) < 1.0) found_2g = true;
        if (std::abs(t.freq_Hz - 2.001e9) < 1.0) found_2001 = true;
    }
    REQUIRE(found_2g);
    REQUIRE(found_2001);
}

TEST_CASE("SParamEngine disconnected input ports produce no contribution",
          "[sparam][multiport]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    // No inputs connected at all
    sp.node().inputs[0] = nullptr;
    sp.node().inputs[1] = nullptr;
    sp.node().inputs[2] = nullptr;

    sp.update(0.0);

    // All outputs should be empty (no tones)
    for (int j = 0; j < 3; ++j) {
        REQUIRE(sp.node().outputs[j].tones.empty());
    }
}

TEST_CASE("SParamEngine multi-port cache invalidates correctly",
          "[sparam][multiport][cache]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    // Wire two inputs
    Spectrum in_a, in_b;
    in_a.tones = {{2e9, -10.0, 0.0}};
    in_a.frequencies = {2e9};
    in_a.noise_W = {1e-12};
    in_a.noise_added_W = {0.0};
    in_a.noise_total_W = {1e-12};
    in_a.generation = 1;

    in_b.tones = {{3e9, -20.0, 0.0}};
    in_b.frequencies = {3e9};
    in_b.noise_W = {1e-12};
    in_b.noise_added_W = {0.0};
    in_b.noise_total_W = {1e-12};
    in_b.generation = 1;

    sp.node().inputs[0] = &in_a;
    sp.node().inputs[2] = &in_b;

    sp.update(0.0);

    auto tone_count = sp.node().outputs[1].tones.size();
    REQUIRE(tone_count > 0);

    // Second update with unchanged inputs should hit cache (same generation)
    sp.update(0.0);
    REQUIRE(sp.node().outputs[1].tones.size() == tone_count);

    // Bump one input's generation — cache should miss
    in_a.generation = 2;
    sp.update(0.0);
    REQUIRE(sp.node().outputs[1].tones.size() == tone_count); // same tone count but should re-evaluate

    // Swap input[0] to new pointer, input[2] still has in_b — cache should miss
    Spectrum in_c;
    in_c.tones = {{4e9, -5.0, 0.0}};
    in_c.frequencies = {4e9};
    in_c.generation = 1;
    sp.node().inputs[0] = &in_c;
    sp.update(0.0);
    REQUIRE(sp.node().outputs[1].tones.size() == 2);

    bool found_new = false;
    for (const auto& t : sp.node().outputs[1].tones) {
        if (std::abs(t.freq_Hz - 4e9) < 1.0) found_new = true;
    }
    REQUIRE(found_new);

    // Remove in_b — now only in_c (4 GHz) on port 1
    sp.node().inputs[2] = nullptr;
    sp.update(0.0);
    REQUIRE(sp.node().outputs[1].tones.size() == 1);
    REQUIRE(sp.node().outputs[1].tones[0].freq_Hz == Approx(4e9));
}

TEST_CASE("SParamEngine splitter mode produces N-1 outputs", "[sparam][multiport]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());
    REQUIRE(sp.numPorts() == 3);

    sp.setCommonPort(1); // Port 2 is COMMON (0-based = 1)
    sp.setMode(SParamEngine::Mode::Splitter);

    REQUIRE(sp.numInputPins() == 1);
    REQUIRE(sp.numOutputPins() == 2);

    Spectrum in_spec;
    in_spec.tones = {{2e9, -10.0, 0.0}};
    in_spec.frequencies = {2e9};
    sp.node().inputs[0] = &in_spec;

    sp.update(0.0);

    REQUIRE(sp.node().outputs[0].tones.size() == 1);
    REQUIRE(sp.node().outputs[1].tones.size() == 1);
}

TEST_CASE("SParamEngine combiner mode sums inputs onto common port",
          "[sparam][multiport]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    sp.setCommonPort(1);
    sp.setMode(SParamEngine::Mode::Combiner);

    REQUIRE(sp.numInputPins() == 2);
    REQUIRE(sp.numOutputPins() == 1);

    Spectrum in_a, in_b;
    in_a.tones = {{2e9, -10.0, 0.0}};
    in_a.frequencies = {2e9};
    in_a.noise_W = {1e-12};
    in_a.noise_total_W = {1e-12};
    in_b.tones = {{2.001e9, -10.0, 0.0}};
    in_b.frequencies = {2.001e9};
    in_b.noise_W = {1e-12};
    in_b.noise_total_W = {1e-12};

    sp.node().inputs[0] = &in_a;
    sp.node().inputs[1] = &in_b;

    sp.update(0.0);

    const auto& out = sp.node().outputs[0];
    REQUIRE(out.tones.size() >= 2);

    bool found_a = false, found_b = false;
    for (const auto& t : out.tones) {
        if (std::abs(t.freq_Hz - 2e9) < 1.0) found_a = true;
        if (std::abs(t.freq_Hz - 2.001e9) < 1.0) found_b = true;
    }
    REQUIRE(found_a);
    REQUIRE(found_b);
}

TEST_CASE("SParamEngine mode switch rebuilds pin layout", "[sparam][multiport]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    REQUIRE(sp.numInputPins() == 3);
    REQUIRE(sp.numOutputPins() == 3);

    sp.setMode(SParamEngine::Mode::Splitter);
    REQUIRE(sp.numInputPins() == 1);
    REQUIRE(sp.numOutputPins() == 2);

    sp.setMode(SParamEngine::Mode::Combiner);
    REQUIRE(sp.numInputPins() == 2);
    REQUIRE(sp.numOutputPins() == 1);

    sp.setMode(SParamEngine::Mode::FullMatrix);
    REQUIRE(sp.numInputPins() == 3);
    REQUIRE(sp.numOutputPins() == 3);
}

TEST_CASE("SParamEngine common port change rebuilds pin layout", "[sparam][multiport]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    sp.setMode(SParamEngine::Mode::Splitter);
    sp.setCommonPort(0);
    REQUIRE(sp.numInputPins() == 1);
    REQUIRE(sp.numOutputPins() == 2);

    sp.setCommonPort(2);
    REQUIRE(sp.numInputPins() == 1);
    REQUIRE(sp.numOutputPins() == 2);
}
