#include "adc_engine.h"
#include "amplifier_engine.h"
#include "combiner_engine.h"
#include "equalizer_engine.h"
#include "ideal_filter_engine.h"
#include "mixer_engine.h"
#include "node_graph_engine.h"
#include "signal_generator_engine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

using Catch::Approx;

namespace {

Spectrum makeSpectrum(std::vector<double> frequencies, std::vector<Spectrum::Tone> tones,
                      bool complex_baseband = false) {
    Spectrum spectrum;
    spectrum.frequencies = std::move(frequencies);
    spectrum.tones = std::move(tones);
    spectrum.noise_W.assign(spectrum.frequencies.size(), 1e-20);
    spectrum.noise_added_W.assign(spectrum.frequencies.size(), 0.0);
    spectrum.noise_total_W.assign(spectrum.frequencies.size(), 1e-20);
    spectrum.phase_deg.assign(spectrum.frequencies.size(), 0.0);
    spectrum.fs_Hz = 1e9;
    spectrum.is_complex_baseband = complex_baseband;
    return spectrum;
}

std::string amplifierSParamPath() {
    return std::string(PROJECT_SOURCE_DIR) + "/component_data/amplifiers/adm-3844psm/"
                                             "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
}

std::filesystem::path writeOverpoweredCombinerSParam() {
    const auto path = std::filesystem::temp_directory_path() / "rf_sim_issue117_overpowered.s3p";
    std::ofstream file(path);
    REQUIRE(file.is_open());
    file << "# Hz S RI R 50\n";
    file << "1e9";
    for (int i = 0; i < 9; ++i)
        file << " 2.0 0.0";
    file << "\n";
    file.close();
    return path;
}

} // namespace

TEST_CASE("Issue 117: ideal amplifier nonlinear distortion matches S-param gain", "[issue117]") {
    NodeGraphEngine graph;
    const auto input = makeSpectrum({1e9, 2e9}, {{1e9, -30.0, 0.0}});

    AmplifierEngine sparam_amp(0, graph);
    sparam_amp.setSParamFilepath(amplifierSParamPath());
    REQUIRE(sparam_amp.sparamMode());
    const auto s21 = sparam_amp.sparamData().interpolate(1e9, 2);
    const double gain_dB = 20.0 * std::log10(std::abs(s21));

    AmplifierEngine ideal_amp(1, graph);
    ideal_amp.setGain_dB(gain_dB);
    ideal_amp.setOIP2_dBm(60.0);
    ideal_amp.setOIP3_dBm(60.0);
    ideal_amp.setEnableNonlinear(true);

    sparam_amp.setOIP2_dBm(60.0);
    sparam_amp.setOIP3_dBm(60.0);
    sparam_amp.setEnableNonlinear(true);

    ideal_amp.node().inputs[0] = &input;
    sparam_amp.node().inputs[0] = &input;
    ideal_amp.update(0.0);
    sparam_amp.update(0.0);

    const auto &ideal_out = ideal_amp.node().outputs[0];
    const auto &sparam_out = sparam_amp.node().outputs[0];
    REQUIRE(ideal_out.tones.size() == 3);
    REQUIRE(sparam_out.tones.size() == 3);
    REQUIRE(ideal_out.tones[1].power_dBm == Approx(sparam_out.tones[1].power_dBm).margin(1e-9));
    REQUIRE(ideal_out.tones[2].power_dBm == Approx(sparam_out.tones[2].power_dBm).margin(1e-9));
}

TEST_CASE("Issue 117: ideal LPF rejects negative complex-baseband blocker", "[issue117]") {
    NodeGraphEngine graph;
    AdcEngine adc(0, graph);
    adc.setFs_Hz(1e9);
    adc.setDecimation(2);
    adc.setNcoFsFraction(0.0);
    Spectrum input = makeSpectrum({0.0, 500e6}, {{50e6, -10.0, 0.0}, {200e6, -20.0, 0.0}});
    adc.node().inputs[0] = &input;
    adc.update(0.0);

    IdealFilterEngine filter(1, graph);
    filter.setFilterType(FilterType::LPF);
    filter.setCutoff_Hz(100e6);
    filter.node().inputs[0] = &adc.node().outputs[0];
    filter.update(0.0);

    const auto &out = filter.node().outputs[0];
    REQUIRE(out.tones.size() == 2);
    for (const auto &tone : out.tones)
        REQUIRE(std::abs(tone.freq_Hz) == Approx(50e6));
}

TEST_CASE("Issue 117: ideal filter clears stale out-of-band noise", "[issue117]") {
    NodeGraphEngine graph;
    auto input = makeSpectrum({-4e9, -2e9, 0.0, 2e9, 4e9}, {});
    IdealFilterEngine filter(0, graph);
    filter.setFilterType(FilterType::LPF);
    filter.node().inputs[0] = &input;

    filter.setCutoff_Hz(1e9);
    filter.update(0.0);
    filter.setCutoff_Hz(3e9);
    filter.update(0.0);
    filter.setCutoff_Hz(1e9);
    filter.update(0.0);

    const auto &out = filter.node().outputs[0];
    for (size_t i = 0; i < out.frequencies.size(); ++i) {
        if (std::abs(out.frequencies[i]) > 1e9) {
            REQUIRE(out.noise_W[i] == 0.0);
            REQUIRE(out.noise_total_W[i] == 0.0);
        }
    }
}

TEST_CASE("Issue 117: generator sample-rate changes propagate after update", "[issue117]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine generator(0, graph);
    generator.update(0.0);
    generator.setFs_Hz(2e9);
    generator.update(0.0);

    REQUIRE(generator.node().outputs[0].fs_Hz == Approx(2e9));
}

TEST_CASE("Issue 117: equalizer magnitude profile and mixer signed difference", "[issue117]") {
    NodeGraphEngine graph;
    auto input = makeSpectrum({-1e9, 1e9}, {{-1e9, -20.0, 0.0}, {1e9, -20.0, 0.0}});

    EqualizerEngine equalizer(0, graph);
    equalizer.setRefGain_dB(3.0);
    equalizer.setRefFreq_Hz(1e8);
    equalizer.setSlope_dBPerDecade(10.0);
    equalizer.node().inputs[0] = &input;
    equalizer.update(0.0);

    const auto &equalized = equalizer.node().outputs[0];
    REQUIRE(equalized.tones.size() == 2);
    REQUIRE(equalized.tones[0].power_dBm == Approx(equalized.tones[1].power_dBm).margin(1e-9));

    MixerEngine complex_mixer(1, graph);
    complex_mixer.setLoFreq_Hz(100e6);
    auto complex_input = makeSpectrum({-400e6, 400e6}, {{-200e6, -10.0, 0.0}}, true);
    complex_mixer.node().inputs[0] = &complex_input;
    complex_mixer.update(0.0);
    REQUIRE(complex_mixer.node().outputs[0].tones[0].freq_Hz == Approx(-300e6));

    MixerEngine real_mixer(2, graph);
    real_mixer.setLoFreq_Hz(100e6);
    auto real_input = makeSpectrum({0.0, 400e6}, {{-200e6, -10.0, 0.0}}, false);
    real_mixer.node().inputs[0] = &real_input;
    real_mixer.update(0.0);
    REQUIRE(real_mixer.node().outputs[0].tones[0].freq_Hz == Approx(300e6));
}

TEST_CASE("Issue 117: combiner validates ports and clamps added noise", "[issue117]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(0, graph);
    combiner.setSParamFilepath(amplifierSParamPath());
    REQUIRE_FALSE(combiner.sparamMode());

    const auto path = writeOverpoweredCombinerSParam();
    combiner.setSParamFilepath(path.string());
    REQUIRE(combiner.sparamMode());

    auto input0 = makeSpectrum({1e9, 2e9}, {{1e9, -10.0, 0.0}});
    auto input1 = makeSpectrum({1e9, 2e9}, {{1e9, -10.0, 0.0}});
    combiner.node().inputs[0] = &input0;
    combiner.node().inputs[1] = &input1;
    combiner.update(0.0);

    for (double noise : combiner.node().outputs[0].noise_added_W)
        REQUIRE(noise >= 0.0);

    std::filesystem::remove(path);
}
