#include "adc_engine.h"
#include "amplifier_engine.h"
#include "attenuator_engine.h"
#include "coax_cable_engine.h"
#include "combiner_engine.h"
#include "equalizer_engine.h"
#include "ideal_filter_engine.h"
#include "mixer_engine.h"
#include "node_graph_engine.h"
#include "pfb_channelizer_engine.h"
#include "s_parameter_data.h"
#include "signal_generator_engine.h"
#include "spectrum.h"
#include "spectrum_analyzer_engine.h"
#include "splitter_engine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using Catch::Approx;
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
    atten.setSParamFilepath(attenuatorS2pPath());

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

TEST_CASE("CoaxCable: propagates is_complex_baseband", "[domain][coax]") {
    NodeGraphEngine graph;
    CoaxCableEngine coax(0, graph);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    coax.node().inputs[0] = &in;
    coax.update(0.0);

    REQUIRE(coax.node().outputs[0].is_complex_baseband == true);
}

TEST_CASE("Splitter: propagates is_complex_baseband to both outputs", "[domain][splitter]") {
    NodeGraphEngine graph;
    SplitterEngine splitter(0, graph);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    splitter.node().inputs[0] = &in;
    splitter.update(0.0);

    REQUIRE(splitter.node().outputs[0].is_complex_baseband == true);
    REQUIRE(splitter.node().outputs[1].is_complex_baseband == true);
}

TEST_CASE("Mixer: propagates is_complex_baseband", "[domain][mixer]") {
    NodeGraphEngine graph;
    MixerEngine mixer(0, graph);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    mixer.node().inputs[0] = &in;
    mixer.update(0.0);

    REQUIRE(mixer.node().outputs[0].is_complex_baseband == true);
}

TEST_CASE("PFBChannelizer: propagates is_complex_baseband", "[domain][pfb]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);

    Spectrum in;
    in.frequencies.resize(401);
    for (int i = 0; i < 401; ++i)
        in.frequencies[i] = -200e6 + i * 1e6;
    in.noise_total_W.assign(401, 1e-20);
    in.is_complex_baseband = true;

    pfb.setFs_Hz(400e6);
    pfb.node().inputs[0] = &in;
    pfb.update(0.0);

    REQUIRE(pfb.node().outputs[0].is_complex_baseband == true);
}

TEST_CASE("SParameterData::applyToSpectrum propagates is_complex_baseband", "[domain][sparam]") {
    SParameterData sparam;
    bool loaded =
        sparam.load(std::string(PROJECT_SOURCE_DIR) + "/component_data/amplifiers/adm-3844psm/"
                                                      "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p");
    REQUIRE(loaded);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    Spectrum out;
    sparam.applyToSpectrum(in, out, 0); // loaded() == true -> reaches the propagation line

    REQUIRE(out.is_complex_baseband == true);
}

TEST_CASE("SParameterData::applyToSpectrum defaults is_complex_baseband when not loaded",
          "[domain][sparam]") {
    SParameterData sparam; // not loaded()
    Spectrum in;
    in.is_complex_baseband = true;
    Spectrum out;
    sparam.applyToSpectrum(in, out, 0); // early-return path, never reaches the propagation line
    REQUIRE(out.is_complex_baseband == false);
}

TEST_CASE("AdcEngine: output is_complex_baseband is true (populated input)", "[domain][adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(0, graph);
    adc.setFs_Hz(1e9);

    Spectrum in;
    in.frequencies.resize(101);
    for (int i = 0; i < 101; ++i)
        in.frequencies[i] = i * 5e6;
    in.noise_W.assign(101, 1e-20);
    in.noise_added_W.assign(101, 0.0);
    in.noise_total_W.assign(101, 1e-20);
    in.tones.push_back({250e6, -20.0, 45.0});

    adc.node().inputs[0] = &in;
    adc.update(0.0);

    REQUIRE(adc.node().outputs[0].is_complex_baseband == true);
    // Regression pin (mirrors the existing "ADC DDC preserves tone power and phase" assertion in
    // tests/test_adc.cpp — restated here to prove the flag addition didn't touch power math):
    REQUIRE(adc.node().outputs[0].tones.size() == 1);
    REQUIRE(adc.node().outputs[0].tones[0].power_dBm == Approx(-20.0));
    REQUIRE(adc.node().outputs[0].tones[0].phase_deg == Approx(45.0));
}

TEST_CASE("AdcEngine: output is_complex_baseband is true (empty input)", "[domain][adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(1, graph);
    adc.setFs_Hz(1e9);

    adc.node().inputs[0] = nullptr;
    adc.update(0.0);

    REQUIRE(adc.node().outputs[0].is_complex_baseband == true);
    REQUIRE(adc.node().outputs[0].frequencies.empty());
}

TEST_CASE("SpectrumAnalyzer: real-domain tone renders as +-fc half-power pair",
          "[domain][spectrum_analyzer]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);

    Spectrum spec;
    spec.frequencies.resize(41);
    for (int i = 0; i < 41; ++i)
        spec.frequencies[i] = -100e6 + i * 5e6; // bin width 5 MHz, spans -100..+100 MHz
    spec.tones = {{50e6, -20.0, 0.0}};
    spec.noise_total_W.assign(41, 1e-21);
    spec.is_complex_baseband = false;
    spec.generation = 1;

    auto trace = sa.renderSpectrum(spec);

    // Bin for +50 MHz: index (50e6 - (-100e6)) / 5e6 = 30. Bin for -50 MHz: index 10.
    REQUIRE(trace.size() == 41);
    const double expected_power = -20.0 - 10.0 * std::log10(2.0); // ~-23.01 dBm
    REQUIRE_THAT(trace[30], WithinAbs(expected_power, 1.0));
    REQUIRE_THAT(trace[10], WithinAbs(expected_power, 1.0));
    // Neither half-power bin should read the full -20 dBm (that would mean no split happened).
    REQUIRE(trace[30] < -20.0 + 0.5);
}

TEST_CASE("SpectrumAnalyzer: complex-baseband tone renders unchanged (no mirroring)",
          "[domain][spectrum_analyzer]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);

    Spectrum spec;
    spec.frequencies.resize(41);
    for (int i = 0; i < 41; ++i)
        spec.frequencies[i] = -100e6 + i * 5e6;
    spec.tones = {{50e6, -20.0, 0.0}};
    spec.noise_total_W.assign(41, 1e-21);
    spec.is_complex_baseband = true;
    spec.generation = 1;

    auto trace = sa.renderSpectrum(spec);

    REQUIRE(trace.size() == 41);
    REQUIRE_THAT(trace[30], WithinAbs(-20.0, 1.0)); // full power at +50 MHz, unchanged
    // -50 MHz bin should show only the noise floor, not a mirrored tone (well below the tone's
    // own -20 dBm and below the would-be mirror power of ~-23 dBm).
    REQUIRE(trace[10] < -50.0);
}

// ---- fs_Hz propagation (issue #43) ----

TEST_CASE("Mixer: propagates fs_Hz", "[domain][mixer]") {
    NodeGraphEngine graph;
    MixerEngine mixer(0, graph);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_total_W.assign(2, 1e-21);
    in.fs_Hz = 500e6;

    mixer.node().inputs[0] = &in;
    mixer.update(0.0);

    REQUIRE(mixer.node().outputs[0].fs_Hz == Approx(500e6));
}

TEST_CASE("Splitter: propagates fs_Hz to both outputs", "[domain][splitter]") {
    NodeGraphEngine graph;
    SplitterEngine splitter(0, graph);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_total_W.assign(2, 1e-21);
    in.fs_Hz = 500e6;

    splitter.node().inputs[0] = &in;
    splitter.update(0.0);

    REQUIRE(splitter.node().outputs[0].fs_Hz == Approx(500e6));
    REQUIRE(splitter.node().outputs[1].fs_Hz == Approx(500e6));
}

TEST_CASE("CoaxCable: propagates fs_Hz", "[domain][coax]") {
    NodeGraphEngine graph;
    CoaxCableEngine coax(0, graph);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_total_W.assign(2, 1e-21);
    in.fs_Hz = 500e6;

    coax.node().inputs[0] = &in;
    coax.update(0.0);

    REQUIRE(coax.node().outputs[0].fs_Hz == Approx(500e6));
}

TEST_CASE("Amplifier: propagates fs_Hz (ideal gain mode)", "[domain][amplifier]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(10.0);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_total_W.assign(2, 1e-21);
    in.fs_Hz = 500e6;

    amp.node().inputs[0] = &in;
    amp.update(0.0);

    REQUIRE(amp.node().outputs[0].fs_Hz == Approx(500e6));
}

TEST_CASE("Amplifier: propagates fs_Hz (S-param mode)", "[domain][amplifier]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setSParamFilepath(amplifierS2pPath());
    REQUIRE(amp.sparamLoaded());

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_total_W.assign(2, 1e-21);
    in.fs_Hz = 500e6;

    amp.node().inputs[0] = &in;
    amp.update(0.0);

    REQUIRE(amp.node().outputs[0].fs_Hz == Approx(500e6));
}

TEST_CASE("Attenuator: propagates fs_Hz (S-param mode)", "[domain][attenuator]") {
    NodeGraphEngine graph;
    AttenuatorEngine atten(0, graph);
    atten.setSParamFilepath(attenuatorS2pPath());
    REQUIRE(atten.sparamMode());

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_total_W.assign(2, 1e-21);
    in.fs_Hz = 500e6;

    atten.node().inputs[0] = &in;
    atten.update(0.0);

    REQUIRE(atten.node().outputs[0].fs_Hz == Approx(500e6));
}

// Real multi-engine post-ADC chains: fs_Hz must reach the PFB and channels must be populated
// without any manual setFs_Hz() (issue #43 regression tests).

static bool anyChannelHasContent(const PFBChannelizerEngine &pfb) {
    for (const auto &ch : pfb.channels()) {
        if (!ch.tones.empty() || ch.noise_W > 0.0)
            return true;
    }
    return false;
}

TEST_CASE("ADC+Mixer: fs_Hz reaches PFB and channels are populated", "[domain][adc][mixer][pfb]") {
    NodeGraphEngine graph;
    AdcEngine adc(0, graph);
    adc.setFs_Hz(1e9);

    Spectrum in;
    in.frequencies.resize(101);
    for (int i = 0; i < 101; ++i)
        in.frequencies[i] = i * 5e6; // 0..500 MHz
    in.noise_W.assign(101, 1e-20);
    in.noise_added_W.assign(101, 0.0);
    in.noise_total_W.assign(101, 1e-20);
    in.tones.push_back({250e6, -20.0, 0.0}); // Fs/4 -> DDC maps to DC

    adc.node().inputs[0] = &in;
    adc.update(0.0);
    REQUIRE(adc.node().outputs[0].fs_Hz == Approx(500e6)); // Fs/2

    MixerEngine mix(1, graph);
    mix.setLoFreq_Hz(100e6);
    mix.node().inputs[0] = &adc.node().outputs[0];
    mix.update(0.0);
    REQUIRE(mix.node().outputs[0].fs_Hz == Approx(500e6));

    PFBChannelizerEngine pfb(2, graph);
    pfb.setChannelCount(32);
    pfb.node().inputs[0] = &mix.node().outputs[0];
    pfb.update(0.0);

    REQUIRE(pfb.fs_Hz() == Approx(500e6));
    REQUIRE(!pfb.node().outputs[0].frequencies.empty());
    REQUIRE(pfb.channels().size() == 32);
    REQUIRE(anyChannelHasContent(pfb));
}

TEST_CASE("ADC+Amplifier(gain): fs_Hz reaches PFB and channels are populated",
          "[domain][adc][amp][pfb]") {
    NodeGraphEngine graph;
    AdcEngine adc(0, graph);
    adc.setFs_Hz(1e9);

    Spectrum in;
    in.frequencies.resize(101);
    for (int i = 0; i < 101; ++i)
        in.frequencies[i] = i * 5e6; // 0..500 MHz
    in.noise_W.assign(101, 1e-20);
    in.noise_added_W.assign(101, 0.0);
    in.noise_total_W.assign(101, 1e-20);
    in.tones.push_back({250e6, -20.0, 0.0}); // Fs/4 -> DDC maps to DC

    adc.node().inputs[0] = &in;
    adc.update(0.0);

    AmplifierEngine amp(1, graph);
    amp.setGain_dB(10.0);
    amp.node().inputs[0] = &adc.node().outputs[0];
    amp.update(0.0);
    REQUIRE(amp.node().outputs[0].fs_Hz == Approx(500e6));

    PFBChannelizerEngine pfb(2, graph);
    pfb.setChannelCount(32);
    pfb.node().inputs[0] = &amp.node().outputs[0];
    pfb.update(0.0);

    REQUIRE(pfb.fs_Hz() == Approx(500e6));
    REQUIRE(!pfb.node().outputs[0].frequencies.empty());
    REQUIRE(pfb.channels().size() == 32);
    REQUIRE(anyChannelHasContent(pfb));
}
