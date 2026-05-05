#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "common.h"
#include "spectrum.h"
#include "signal_generator_engine.h"
#include "amplifier_engine.h"
#include "splitter_engine.h"
#include "mixer_engine.h"
#include "spectrum_analyzer_engine.h"
#include "node_graph_engine.h"

using Catch::Approx;

TEST_CASE("dB to linear conversion", "[common]") {
    REQUIRE(dbToLinear(0.0) == Approx(1.0));
    REQUIRE(dbToLinear(10.0) == Approx(10.0));
    REQUIRE(dbToLinear(-10.0) == Approx(0.1));
    REQUIRE(dbToLinear(20.0) == Approx(100.0));
}

TEST_CASE("Noise temperature calculation", "[common]") {
    REQUIRE(calculateNoiseTemp(0.0) == Approx(0.0));
    REQUIRE(calculateNoiseTemp(3.0) == Approx(290.0).epsilon(0.01));
}

TEST_CASE("Added noise density", "[common]") {
    double added = addedNoiseDensity_W_per_Hz(0.0, 1.0);
    REQUIRE(added == Approx(0.0));

    double added2 = addedNoiseDensity_W_per_Hz(10.0, dbToLinear(10.0));
    double expected2 = k * calculateNoiseTemp(10.0) * dbToLinear(10.0);
    REQUIRE(added2 == Approx(expected2).epsilon(0.001));
}

TEST_CASE("Added noise per bin (deprecated helper)", "[common]") {
    double density = addedNoiseDensity_W_per_Hz(10.0, dbToLinear(10.0));
    double per_bin = addedNoisePerBin_W(10.0, dbToLinear(10.0), 1e6);
    REQUIRE(per_bin == Approx(density * 1e6).epsilon(0.001));
}

TEST_CASE("Spectrum computeTotalNoise", "[common]") {
    Spectrum spec;
    const int N = 5;
    spec.frequencies.resize(N);
    for (int i = 0; i < N; ++i) {
        spec.frequencies[i] = i * 1e6;
    }
    // Values are now W/Hz (density)
    spec.noise_W = {1e-18, 2e-18, 3e-18, 4e-18, 5e-18};
    spec.noise_added_W = {0.5e-18, 0.6e-18, 0.7e-18, 0.8e-18, 0.9e-18};
    spec.computeTotalNoise();
    REQUIRE(spec.noise_total_W.size() == N);
    for (int i = 0; i < N; ++i) {
        REQUIRE(spec.noise_total_W[i] == Approx(spec.noise_W[i] + spec.noise_added_W[i]).epsilon(1e-30));
    }
}

TEST_CASE("Generator outputs flat thermal noise density", "[generator]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);

    const auto &out = gen.node().outputs[0];
    REQUIRE(!out.noise_total_W.empty());
    for (double density : out.noise_total_W) {
        REQUIRE(density == Approx(k * T).epsilon(1e-30));
    }
}

TEST_CASE("Generator with no tones produces empty tone list", "[generator]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);
    REQUIRE(gen.node().outputs[0].tones.empty());
    REQUIRE(gen.toneCount() == 0);
}

TEST_CASE("Generator with multiple tones outputs all tones", "[generator]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.addTone(200e6, -10.0);
    gen.addTone(50e6, 0.0);
    gen.update(0.0);

    REQUIRE(gen.toneCount() == 3);
    const auto &tones = gen.node().outputs[0].tones;
    REQUIRE(tones.size() == 3);
    REQUIRE(tones[0].freq_Hz == 100e6);
    REQUIRE(tones[0].power_dBm == -20.0);
    REQUIRE(tones[1].freq_Hz == 200e6);
    REQUIRE(tones[1].power_dBm == -10.0);
    REQUIRE(tones[2].freq_Hz == 50e6);
    REQUIRE(tones[2].power_dBm == 0.0);
}

TEST_CASE("Generator removeTone works correctly", "[generator]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.addTone(200e6, -10.0);
    gen.removeTone(0);
    gen.update(0.0);

    REQUIRE(gen.toneCount() == 1);
    REQUIRE(gen.node().outputs[0].tones.size() == 1);
    REQUIRE(gen.node().outputs[0].tones[0].freq_Hz == 200e6);
}

TEST_CASE("Generator updateTone modifies existing tone", "[generator]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.updateTone(0, 150e6, -5.0);
    gen.update(0.0);

    REQUIRE(gen.toneCount() == 1);
    REQUIRE(gen.node().outputs[0].tones[0].freq_Hz == 150e6);
    REQUIRE(gen.node().outputs[0].tones[0].power_dBm == -5.0);
}

TEST_CASE("Generator addTone with phase", "[generator][phase]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0, 45.0);
    gen.update(0.0);

    REQUIRE(gen.toneCount() == 1);
    REQUIRE(gen.node().outputs[0].tones[0].freq_Hz == 100e6);
    REQUIRE(gen.node().outputs[0].tones[0].power_dBm == -20.0);
    REQUIRE(gen.node().outputs[0].tones[0].phase_deg == 45.0);
}

TEST_CASE("Generator updateTone phase", "[generator][phase]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.updateTone(0, 100e6, -20.0, 90.0);
    gen.update(0.0);

    REQUIRE(gen.node().outputs[0].tones[0].phase_deg == 90.0);
}

TEST_CASE("Generator phase_deg is zeroed per bin", "[generator][phase]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);

    const auto &out = gen.node().outputs[0];
    REQUIRE(out.phase_deg.size() == out.frequencies.size());
    for (double p : out.phase_deg) {
        REQUIRE(p == 0.0);
    }
}

TEST_CASE("Amplifier propagates tone phase", "[amplifier][phase]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0, 45.0);
    gen.update(0.0);

    AmplifierEngine amp(0, graph);
    amp.setGain_dB(10.0);
    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    const auto &out = amp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].phase_deg == 45.0);
}

TEST_CASE("Noise floor remains k*T regardless of tone count", "[generator]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    // No tones - just noise
    gen.update(0.0);
    for (double density : gen.node().outputs[0].noise_total_W) {
        REQUIRE(density == Catch::Approx(k * T).epsilon(1e-30));
    }

    // Add multiple tones - noise should still be k*T
    gen.addTone(100e6, -20.0);
    gen.addTone(200e6, -10.0);
    gen.addTone(300e6, 0.0);
    gen.update(0.0);
    for (double density : gen.node().outputs[0].noise_total_W) {
        REQUIRE(density == Catch::Approx(k * T).epsilon(1e-30));
    }
}

TEST_CASE("Splitter produces two outputs with -3dB tones", "[splitter]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    SplitterEngine split(0, graph);
    split.node().inputs[0] = &gen.node().outputs[0];
    split.update(0.0);

    REQUIRE(split.node().outputs.size() == 2);
    for (size_t out_idx = 0; out_idx < 2; ++out_idx) {
        const auto& out = split.node().outputs[out_idx];
        REQUIRE(out.tones.size() == 1);
        REQUIRE(out.tones[0].freq_Hz == 100e6);
        REQUIRE(out.tones[0].power_dBm == Approx(-20.0 - 3.0103).epsilon(0.001));
    }
}

TEST_CASE("Splitter preserves phase on both outputs", "[splitter]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0, 45.0);
    gen.update(0.0);

    SplitterEngine split(0, graph);
    split.node().inputs[0] = &gen.node().outputs[0];
    split.update(0.0);

    for (size_t out_idx = 0; out_idx < 2; ++out_idx) {
        const auto& out = split.node().outputs[out_idx];
        REQUIRE(out.tones[0].phase_deg == 45.0);
    }
}

TEST_CASE("Splitter scales noise density by -3dB", "[splitter]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);

    SplitterEngine split(0, graph);
    split.node().inputs[0] = &gen.node().outputs[0];
    split.update(0.0);

    double expected_density = (k * T) / 2.0;
    for (size_t out_idx = 0; out_idx < 2; ++out_idx) {
        const auto& out = split.node().outputs[out_idx];
        REQUIRE(!out.noise_total_W.empty());
        for (double density : out.noise_total_W) {
            REQUIRE(density == Approx(expected_density).epsilon(1e-30));
        }
    }
}

TEST_CASE("Mixer produces sum and difference tones", "[mixer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    MixerEngine mix(0, graph);
    mix.setLoFreq_Hz(80e6);
    mix.setConversionGain_dB(-6.0);
    mix.node().inputs[0] = &gen.node().outputs[0];
    mix.update(0.0);

    const auto& out = mix.node().outputs[0];
    REQUIRE(out.tones.size() == 2);
    // Lower sideband: |100 - 80| = 20 MHz
    REQUIRE(out.tones[0].freq_Hz == 20e6);
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0 - 6.0).epsilon(0.001));
    // Upper sideband: 100 + 80 = 180 MHz
    REQUIRE(out.tones[1].freq_Hz == 180e6);
    REQUIRE(out.tones[1].power_dBm == Approx(-20.0 - 6.0).epsilon(0.001));
}

TEST_CASE("Mixer preserves phase on sidebands", "[mixer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0, 45.0);
    gen.update(0.0);

    MixerEngine mix(0, graph);
    mix.setLoFreq_Hz(80e6);
    mix.node().inputs[0] = &gen.node().outputs[0];
    mix.update(0.0);

    const auto& out = mix.node().outputs[0];
    REQUIRE(out.tones.size() == 2);
    REQUIRE(out.tones[0].phase_deg == 45.0);
    REQUIRE(out.tones[1].phase_deg == 45.0);
}

TEST_CASE("Mixer scales noise by conversion gain", "[mixer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);

    MixerEngine mix(0, graph);
    mix.setConversionGain_dB(-6.0);
    mix.node().inputs[0] = &gen.node().outputs[0];
    mix.update(0.0);

    double G = dbToLinear(-6.0);
    double expected_density = k * T * G;
    const auto& out = mix.node().outputs[0];
    REQUIRE(!out.noise_total_W.empty());
    for (double density : out.noise_total_W) {
        REQUIRE(density == Approx(expected_density).epsilon(1e-30));
    }
}

TEST_CASE("Mixer with no input produces no tones", "[mixer]") {
    NodeGraphEngine graph;
    MixerEngine mix(0, graph);
    mix.setLoFreq_Hz(1e9);
    mix.setConversionGain_dB(-6.0);
    mix.update(0.0);

    const auto& out = mix.node().outputs[0];
    REQUIRE(out.tones.empty());
}

TEST_CASE("Amplifier scales noise density correctly", "[amplifier]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);

    AmplifierEngine amp(0, graph);
    amp.setGain_dB(10.0);
    amp.setNF_dB(3.0);
    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    const auto &out = amp.node().outputs[0];
    REQUIRE(!out.noise_total_W.empty());

    double G = dbToLinear(10.0);
    double Te = calculateNoiseTemp(3.0);
    double expected_density = k * T * G + k * Te * G;

    for (double density : out.noise_total_W) {
        REQUIRE(density == Approx(expected_density).epsilon(1e-30));
    }
}

TEST_CASE("Spectrum analyzer noise floor depends on RBW not grid spacing", "[spectrum]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);

    AmplifierEngine amp(0, graph);
    amp.setGain_dB(20.0);
    amp.setNF_dB(5.0);
    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    SpectrumAnalyzerEngine sa;
    sa.setStartFrequency(MIN_FREQ);
    sa.setStopFrequency(MAX_FREQ);
    sa.setResBw(50e6);
    sa.setNoiseJitterEnabled(false);

    std::vector<const Spectrum *> specs = {&amp.node().outputs[0]};
    auto display1 = sa.renderCombinedSpectrum(specs);

    // Re-run with same settings; noise floor should remain consistent
    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    auto display2 = sa.renderCombinedSpectrum(specs);

    size_t mid = std::min(display1.size(), display2.size()) / 2;
    REQUIRE(display1[mid] == Approx(display2[mid]).epsilon(1.0));

    sa.setResBw(100e6);
    auto display3 = sa.renderCombinedSpectrum(specs);

    REQUIRE(display3[mid] > display2[mid]);
}

TEST_CASE("findPeaks detects single tone peak", "[spectrum]") {
    SpectrumAnalyzerEngine sa;
    std::vector<double> freq = {0, 1e6, 2e6, 3e6, 4e6};
    std::vector<double> power = {-80.0, -70.0, -60.0, -70.0, -80.0};

    auto peaks = sa.findPeaks(power, freq);
    REQUIRE(peaks.size() == 1);
    REQUIRE(peaks[0].index == 2);
    REQUIRE(peaks[0].freq_Hz == 2e6);
    REQUIRE(peaks[0].power_dBm == -60.0);
}

TEST_CASE("findPeaks handles multiple peaks", "[spectrum]") {
    SpectrumAnalyzerEngine sa;
    std::vector<double> freq = {0, 1e6, 2e6, 3e6, 4e6, 5e6, 6e6};
    std::vector<double> power = {-90.0, -50.0, -60.0, -30.0, -55.0, -40.0, -90.0};

    SECTION("sorts by descending power") {
        auto peaks = sa.findPeaks(power, freq);
        REQUIRE(peaks.size() == 3);
        REQUIRE(peaks[0].power_dBm == -30.0);
        REQUIRE(peaks[0].freq_Hz == 3e6);
        REQUIRE(peaks[0].index == 3);
        REQUIRE(peaks[1].power_dBm == -40.0);
        REQUIRE(peaks[1].freq_Hz == 5e6);
        REQUIRE(peaks[1].index == 5);
        REQUIRE(peaks[2].power_dBm == -50.0);
        REQUIRE(peaks[2].freq_Hz == 1e6);
        REQUIRE(peaks[2].index == 1);
    }

    SECTION("limits to max_count") {
        auto peaks = sa.findPeaks(power, freq, 2);
        REQUIRE(peaks.size() == 2);
        REQUIRE(peaks[0].power_dBm == -30.0);
        REQUIRE(peaks[1].power_dBm == -40.0);
    }
}

TEST_CASE("findPeaks returns empty for flat spectrum", "[spectrum]") {
    SpectrumAnalyzerEngine sa;
    std::vector<double> freq = {0, 1e6, 2e6, 3e6};
    std::vector<double> power = {-80.0, -80.0, -80.0, -80.0};

    auto peaks = sa.findPeaks(power, freq);
    REQUIRE(peaks.empty());
}

TEST_CASE("findPeaks skips endpoints", "[spectrum]") {
    SpectrumAnalyzerEngine sa;
    std::vector<double> freq = {0, 1e6, 2e6};
    std::vector<double> power = {-30.0, -80.0, -30.0};

    auto peaks = sa.findPeaks(power, freq);
    REQUIRE(peaks.empty());
}

TEST_CASE("findPeaks returns empty for too few points", "[spectrum]") {
    SpectrumAnalyzerEngine sa;
    std::vector<double> freq = {0, 1e6};
    std::vector<double> power = {-30.0, -80.0};

    auto peaks = sa.findPeaks(power, freq);
    REQUIRE(peaks.empty());
}

TEST_CASE("findPeaks returns empty for empty input", "[spectrum]") {
    SpectrumAnalyzerEngine sa;
    std::vector<double> freq;
    std::vector<double> power;
    auto peaks = sa.findPeaks(power, freq);
    REQUIRE(peaks.empty());
}

TEST_CASE("findPeaks returns empty for size mismatch", "[spectrum]") {
    SpectrumAnalyzerEngine sa;
    std::vector<double> freq = {0, 1e6, 2e6};
    std::vector<double> power = {-80.0, -70.0};
    auto peaks = sa.findPeaks(power, freq);
    REQUIRE(peaks.empty());
}

TEST_CASE("findPeaks detects peak in minimum 3-point spectrum", "[spectrum]") {
    SpectrumAnalyzerEngine sa;
    std::vector<double> freq = {0, 1e6, 2e6};
    std::vector<double> power = {-80.0, -30.0, -80.0};
    auto peaks = sa.findPeaks(power, freq);
    REQUIRE(peaks.size() == 1);
    REQUIRE(peaks[0].index == 1);
    REQUIRE(peaks[0].freq_Hz == 1e6);
    REQUIRE(peaks[0].power_dBm == -30.0);
}
