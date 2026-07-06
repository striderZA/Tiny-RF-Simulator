#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "amplifier_engine.h"
#include "signal_generator_engine.h"
#include "node_graph_engine.h"
#include <cmath>
#include <numbers>

using Catch::Approx;

static std::string s2p_path() {
    return std::string(PROJECT_SOURCE_DIR) +
        "/component_data/amplifiers/adm-3844psm/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
}

TEST_CASE("Amplifier ideal mode unchanged after S-param refactor", "[amp][sparam]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(15.0);

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    REQUIRE(amp.node().outputs[0].tones.size() == 1);
    REQUIRE(amp.node().outputs[0].tones[0].power_dBm == Approx(-5.0).margin(0.01));
}

TEST_CASE("Amplifier S-param mode loads .s2p and applies S21 gain", "[amp][sparam]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setSParamFilepath(s2p_path());

    REQUIRE(amp.sparamLoaded());
    REQUIRE(amp.sparamMode());
    REQUIRE(amp.sparamData().numPorts() == 2);
    REQUIRE(amp.sparamData().freqs().size() > 10);

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    const auto& out = amp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);

    // S21 at 1 GHz on this amp gives ~19.6 dB gain
    auto S21 = amp.sparamData().interpolate(1e9, 2);
    double expected_gain = 20.0 * std::log10(std::abs(S21));
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0 + expected_gain).margin(0.5));
    REQUIRE(out.tones[0].phase_deg == Approx(std::arg(S21) * 180.0 / std::numbers::pi).margin(1.0));
}

TEST_CASE("Amplifier S-param applies phase rotation", "[amp][sparam]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setSParamFilepath(s2p_path());
    REQUIRE(amp.sparamLoaded());

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    const auto& out = amp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    auto S21 = amp.sparamData().interpolate(1e9, 2);
    double expected_phase = std::arg(S21) * 180.0 / std::numbers::pi;
    REQUIRE(out.tones[0].phase_deg == Approx(expected_phase).margin(1.0));
}

TEST_CASE("Amplifier S-param interpolates between data points", "[amp][sparam]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setSParamFilepath(s2p_path());
    REQUIRE(amp.sparamLoaded());

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(1.005e9, -20.0);
    gen.update(0.0);

    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    const auto& out = amp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    auto S21 = amp.sparamData().interpolate(1.005e9, 2);
    double expected_gain = 20.0 * std::log10(std::abs(S21));
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0 + expected_gain).margin(0.5));
}

TEST_CASE("Amplifier S-param handles out-of-band frequency", "[amp][sparam]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setSParamFilepath(s2p_path());
    REQUIRE(amp.sparamLoaded());

    SignalGeneratorEngine gen(1, graph);
    double f_outside = amp.sparamData().freqs().back() + 10e9;
    gen.addTone(f_outside, -20.0);
    gen.update(0.0);

    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    const auto& out = amp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    auto S21 = amp.sparamData().interpolate(f_outside, 2);
    double expected_phase = std::arg(S21) * 180.0 / std::numbers::pi;
    REQUIRE(out.tones[0].phase_deg == Approx(expected_phase).margin(0.1));
}

TEST_CASE("Amplifier S-param adds noise figure correctly", "[amp][sparam][nf]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setSParamFilepath(s2p_path());
    REQUIRE(amp.sparamLoaded());

    SignalGeneratorEngine gen(1, graph);
    gen.update(0.0);
    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    // NF = 0: noise_added_W should be zero
    amp.setNF_dB(0.0);
    amp.update(0.0);
    const auto& out0 = amp.node().outputs[0];
    REQUIRE(!out0.noise_added_W.empty());
    for (double n : out0.noise_added_W)
        REQUIRE(n == Approx(0.0).margin(1e-30));

    // NF = 3 dB: noise_added_W should be positive
    amp.setNF_dB(3.0);
    amp.update(0.0);
    const auto& out3 = amp.node().outputs[0];
    bool any_positive = false;
    for (double n : out3.noise_added_W) {
        if (n > 0.0) { any_positive = true; break; }
    }
    REQUIRE(any_positive);
}

TEST_CASE("Amplifier S-param nonlinear creates harmonics", "[amp][sparam][nonlinear]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setSParamFilepath(s2p_path());
    REQUIRE(amp.sparamLoaded());

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(100e6, -40.0);
    gen.update(0.0);

    amp.setOIP2_dBm(40.0);
    amp.setOIP3_dBm(30.0);
    amp.setEnableNonlinear(true);
    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    const auto& out = amp.node().outputs[0];
    REQUIRE(out.tones.size() >= 3);

    bool found_h2 = false, found_h3 = false;
    for (const auto& t : out.tones) {
        if (std::abs(t.freq_Hz - 200e6) < 1.0) found_h2 = true;
        if (std::abs(t.freq_Hz - 300e6) < 1.0) found_h3 = true;
    }
    REQUIRE(found_h2);
    REQUIRE(found_h3);
}

TEST_CASE("Amplifier S-param handles bad file gracefully", "[amp][sparam]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setSParamFilepath("/nonexistent/file.s2p");
    REQUIRE(!amp.sparamLoaded());
    REQUIRE(!amp.sparamMode());
    amp.update(0.0); // should not crash
}
