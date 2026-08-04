#include "amplifier_engine.h"
#include "attenuator_engine.h"
#include "combiner_engine.h"
#include "equalizer_engine.h"
#include "ideal_filter_engine.h"
#include "node_graph_engine.h"
#include "signal_generator_engine.h"
#include "spectrum.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using Catch::Matchers::WithinAbs;

TEST_CASE("Spectrum: is_complex_baseband defaults to false", "[spectrum][domain]") {
    Spectrum s;
    REQUIRE(s.is_complex_baseband == false);
}

TEST_CASE("conjugateSymmetricExpand: splits a real tone into +-fc half-power pair",
          "[spectrum][euler]") {
    std::vector<Spectrum::Tone> in = {{250e6, -20.0, 45.0}};
    auto out = conjugateSymmetricExpand(in);

    REQUIRE(out.size() == 2);

    const double expected_power = -20.0 - 10.0 * std::log10(2.0); // -23.0103 dBm

    bool found_pos = false, found_neg = false;
    for (const auto &t : out) {
        if (t.freq_Hz > 0.0) {
            found_pos = true;
            REQUIRE_THAT(t.freq_Hz, WithinAbs(250e6, 1e-6));
            REQUIRE_THAT(t.power_dBm, WithinAbs(expected_power, 1e-9));
            REQUIRE_THAT(t.phase_deg, WithinAbs(45.0, 1e-9));
        } else {
            found_neg = true;
            REQUIRE_THAT(t.freq_Hz, WithinAbs(-250e6, 1e-6));
            REQUIRE_THAT(t.power_dBm, WithinAbs(expected_power, 1e-9));
            REQUIRE_THAT(t.phase_deg, WithinAbs(45.0, 1e-9));
        }
    }
    REQUIRE(found_pos);
    REQUIRE(found_neg);
}

TEST_CASE("conjugateSymmetricExpand: DC tone is not mirrored or split", "[spectrum][euler]") {
    std::vector<Spectrum::Tone> in = {{0.0, -10.0, 0.0}};
    auto out = conjugateSymmetricExpand(in);

    REQUIRE(out.size() == 1);
    REQUIRE(out[0].freq_Hz == 0.0);
    REQUIRE_THAT(out[0].power_dBm, WithinAbs(-10.0, 1e-9));
}

TEST_CASE("conjugateSymmetricExpand: multiple tones each expand independently",
          "[spectrum][euler]") {
    std::vector<Spectrum::Tone> in = {{100e6, -10.0, 0.0}, {0.0, -5.0, 0.0}, {200e6, -30.0, 90.0}};
    auto out = conjugateSymmetricExpand(in);

    // Two real tones (2 mirrors each) + one DC tone (unmirrored) = 5 entries
    REQUIRE(out.size() == 5);
}

TEST_CASE("conjugateSymmetricExpand: empty input produces empty output", "[spectrum][euler]") {
    std::vector<Spectrum::Tone> in;
    auto out = conjugateSymmetricExpand(in);
    REQUIRE(out.empty());
}

namespace {
std::string amplifierS2pPath() {
    return std::string(PROJECT_SOURCE_DIR) + "/component_data/amplifiers/adm-3844psm/"
                                             "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
}
std::string attenuatorS2pPath() {
    return std::string(PROJECT_SOURCE_DIR) +
           "/component_data/fixed_attenuators/atn01-0040psm/ATN01-0040PSM_SM_25C_De.s2p";
}
} // namespace

TEST_CASE("SignalGenerator: output is_complex_baseband is always false", "[domain][generator]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);
    REQUIRE(gen.node().outputs[0].is_complex_baseband == false);
}

TEST_CASE("Amplifier: propagates is_complex_baseband (ideal mode)", "[domain][amplifier]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_W.assign(2, 1e-21);
    in.noise_added_W.assign(2, 0.0);
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    amp.node().inputs[0] = &in;
    amp.update(0.0);

    REQUIRE(amp.node().outputs[0].is_complex_baseband == true);
}

TEST_CASE("Amplifier: propagates is_complex_baseband (S-param mode)", "[domain][amplifier]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setSParamFilepath(amplifierS2pPath());
    REQUIRE(amp.sparamMode());

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_W.assign(2, 1e-21);
    in.noise_added_W.assign(2, 0.0);
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    amp.node().inputs[0] = &in;
    amp.update(0.0);

    REQUIRE(amp.node().outputs[0].is_complex_baseband == true);
}

TEST_CASE("Attenuator: propagates is_complex_baseband (manual mode)", "[domain][attenuator]") {
    NodeGraphEngine graph;
    AttenuatorEngine atten(0, graph);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_W.assign(2, 1e-21);
    in.noise_added_W.assign(2, 0.0);
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    atten.node().inputs[0] = &in;
    atten.update(0.0);

    REQUIRE(atten.node().outputs[0].is_complex_baseband == true);
}

TEST_CASE("Attenuator: propagates is_complex_baseband (S-param mode)", "[domain][attenuator]") {
    NodeGraphEngine graph;
    AttenuatorEngine atten(0, graph);
    atten.setSParamFile(attenuatorS2pPath());

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_W.assign(2, 1e-21);
    in.noise_added_W.assign(2, 0.0);
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    atten.node().inputs[0] = &in;
    atten.update(0.0);

    REQUIRE(atten.node().outputs[0].is_complex_baseband == true);
}

TEST_CASE("SignalGenerator+Amplifier: false flag also propagates", "[domain][amplifier]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);

    Spectrum in; // is_complex_baseband defaults false
    in.frequencies = {1e9, 2e9};
    in.noise_W.assign(2, 1e-21);
    in.noise_added_W.assign(2, 0.0);
    in.noise_total_W.assign(2, 1e-21);

    amp.node().inputs[0] = &in;
    amp.update(0.0);

    REQUIRE(amp.node().outputs[0].is_complex_baseband == false);
}

TEST_CASE("Combiner: propagates is_complex_baseband (manual mode, either input)",
          "[domain][combiner]") {
    NodeGraphEngine graph;
    CombinerEngine comb(0, graph);

    Spectrum in0, in1;
    in0.frequencies = {1e9, 2e9};
    in0.noise_total_W.assign(2, 1e-21);
    in0.is_complex_baseband = true;
    in1.frequencies = {1e9, 2e9};
    in1.noise_total_W.assign(2, 1e-21);
    in1.is_complex_baseband = false;

    comb.node().inputs[0] = &in0;
    comb.node().inputs[1] = &in1;
    comb.update(0.0);

    REQUIRE(comb.node().outputs[0].is_complex_baseband == true);
}

TEST_CASE("Equalizer: propagates is_complex_baseband (ideal mode)", "[domain][equalizer]") {
    NodeGraphEngine graph;
    EqualizerEngine eq(0, graph);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    eq.node().inputs[0] = &in;
    eq.update(0.0);

    REQUIRE(eq.node().outputs[0].is_complex_baseband == true);
}

TEST_CASE("IdealFilter: propagates is_complex_baseband", "[domain][ideal_filter]") {
    NodeGraphEngine graph;
    IdealFilterEngine flt(0, graph);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    flt.node().inputs[0] = &in;
    flt.update(0.0);

    REQUIRE(flt.node().outputs[0].is_complex_baseband == true);
}
