#include "amplifier_engine.h"
#include "common.h"
#include "ideal_filter_engine.h"
#include "mixer_engine.h"
#include "node_graph_engine.h"
#include "signal_generator_engine.h"
#include "spectrum.h"
#include "spectrum_analyzer_engine.h"
#include "splitter_engine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
        REQUIRE(spec.noise_total_W[i] ==
                Approx(spec.noise_W[i] + spec.noise_added_W[i]).epsilon(1e-30));
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
        const auto &out = split.node().outputs[out_idx];
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
        const auto &out = split.node().outputs[out_idx];
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
        const auto &out = split.node().outputs[out_idx];
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

    const auto &out = mix.node().outputs[0];
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

    const auto &out = mix.node().outputs[0];
    REQUIRE(out.tones.size() == 2);
    REQUIRE(out.tones[0].phase_deg == 45.0);
    REQUIRE(out.tones[1].phase_deg == 45.0);
}

TEST_CASE("Mixer translates noise into both LO sidebands", "[mixer][domain]") {
    NodeGraphEngine graph;
    Spectrum input;
    for (int i = 0; i <= 40; ++i)
        input.frequencies.push_back(i * 10e6);
    input.noise_total_W.assign(input.frequencies.size(), 0.0);
    input.noise_total_W[10] = 1e-12;

    MixerEngine mix(0, graph);
    mix.setLoFreq_Hz(100e6);
    mix.setConversionGain_dB(-6.0);
    mix.node().inputs[0] = &input;
    mix.update(0.0);

    const auto &out = mix.node().outputs[0];
    const double translated_density = 1e-12 * dbToLinear(-6.0);
    REQUIRE(out.noise_total_W[0] == Approx(translated_density));
    REQUIRE(out.noise_total_W[20] == Approx(translated_density));
    REQUIRE(out.noise_total_W[10] == 0.0);
}

TEST_CASE("Mixer preserves RF noise SNR through an IF filter", "[mixer][domain]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1e9, -10.0);
    gen.update(0.0);

    IdealFilterEngine rf_filter(1, graph);
    rf_filter.setFilterType(FilterType::BPF);
    rf_filter.setCutoffs_Hz(950e6, 1050e6);
    rf_filter.node().inputs[0] = &gen.node().outputs[0];
    rf_filter.update(0.0);

    MixerEngine mix(2, graph);
    mix.setLoFreq_Hz(900e6);
    mix.setConversionGain_dB(0.0);
    mix.node().inputs[0] = &rf_filter.node().outputs[0];
    mix.update(0.0);

    IdealFilterEngine if_filter(3, graph);
    if_filter.setFilterType(FilterType::LPF);
    if_filter.setCutoff_Hz(150e6);
    if_filter.node().inputs[0] = &mix.node().outputs[0];
    if_filter.update(0.0);

    const auto &rf = rf_filter.node().outputs[0];
    const auto &if_out = if_filter.node().outputs[0];
    double rf_noise_W = 0.0;
    double if_noise_W = 0.0;
    for (size_t i = 0; i < rf.frequencies.size(); ++i) {
        if (rf.frequencies[i] >= 950e6 && rf.frequencies[i] <= 1050e6)
            rf_noise_W += rf.noise_total_W[i] * 10e6;
    }
    for (size_t i = 0; i < if_out.frequencies.size(); ++i) {
        if (if_out.frequencies[i] >= 50e6 && if_out.frequencies[i] <= 150e6)
            if_noise_W += if_out.noise_total_W[i] * 10e6;
    }

    const double rf_snr_dB = -10.0 - (10.0 * std::log10(rf_noise_W) + 30.0);
    const double if_snr_dB = -10.0 - (10.0 * std::log10(if_noise_W) + 30.0);
    REQUIRE(10.0 * std::log10(if_noise_W / (k * T * 100e6)) == Approx(0.0).margin(0.5));
    REQUIRE(if_snr_dB == Approx(rf_snr_dB).margin(0.5));
}

TEST_CASE("Mixer with no input produces no tones", "[mixer]") {
    NodeGraphEngine graph;
    MixerEngine mix(0, graph);
    mix.setLoFreq_Hz(1e9);
    mix.setConversionGain_dB(-6.0);
    mix.update(0.0);

    const auto &out = mix.node().outputs[0];
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

TEST_CASE("Spectrum analyzer noise floor tracks effective RBW", "[spectrum]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);

    const Spectrum &spec = gen.node().outputs[0];
    const std::vector<const Spectrum *> specs = {&spec};
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);

    const size_t mid = spec.frequencies.size() / 2;
    for (double rbw : {1e3, 1e6, 10e6, 50e6, 100e6}) {
        sa.setResBw(rbw);
        const double expected_dBm = 10.0 * std::log10(k * T * rbw) + 30.0;

        const auto trace = sa.renderSpectrum(spec);
        const auto combined = sa.renderCombinedSpectrum(specs);
        const double average_noise_dBm = sa.computeAverageNoiseLevel(specs);

        INFO("RBW: " << rbw);
        REQUIRE(trace.front() == Approx(expected_dBm).margin(0.02));
        REQUIRE(trace[mid] == Approx(expected_dBm).margin(0.02));
        REQUIRE(trace.back() == Approx(expected_dBm).margin(0.02));
        REQUIRE(combined.front() == Approx(expected_dBm).margin(0.02));
        REQUIRE(combined[mid] == Approx(expected_dBm).margin(0.02));
        REQUIRE(combined.back() == Approx(expected_dBm).margin(0.02));
        REQUIRE(average_noise_dBm == Approx(expected_dBm).margin(0.005));
    }
}

TEST_CASE("Spectrum analyzer RBW filtering preserves tone peak power", "[spectrum]") {
    Spectrum spec;
    spec.frequencies = {-10e6, 0.0, 10e6};
    spec.tones = {{0.0, -20.0, 0.0}};

    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setVideoBw(1e6);

    for (double rbw : {1e3, 100e6}) {
        sa.setResBw(rbw);
        const auto trace = sa.renderSpectrum(spec);
        REQUIRE(trace[1] == Approx(-20.0).margin(0.01));
    }
}

TEST_CASE("Spectrum analyzer RBW changes reset held traces", "[spectrum][trace]") {
    Spectrum spec;
    for (size_t i = 0; i <= 100; ++i) {
        spec.frequencies.push_back((static_cast<double>(i) - 50.0) * 10e6);
    }
    spec.noise_total_W.assign(spec.frequencies.size(), k * T);
    const size_t mid = spec.frequencies.size() / 2;
    const auto expected_dBm = [](double rbw) { return 10.0 * std::log10(k * T * rbw) + 30.0; };

    const auto check_change = [&](TraceMode mode, double initial_rbw, double changed_rbw) {
        SpectrumAnalyzerEngine sa;
        sa.setNoiseJitterEnabled(false);
        sa.setVideoBw(1e6);
        sa.setVideoAvgCount(5);
        sa.setTraceMode(mode);
        sa.setResBw(initial_rbw);

        const auto first = sa.renderSpectrum(spec);
        REQUIRE(first[mid] == Approx(expected_dBm(initial_rbw)).margin(0.02));

        sa.setResBw(changed_rbw);
        const auto changed = sa.renderSpectrum(spec);
        REQUIRE(changed[mid] == Approx(expected_dBm(changed_rbw)).margin(0.02));
    };

    SECTION("Max Hold drops the old wider-band maximum") {
        check_change(TraceMode::MaxHold, 100e6, 20e6);
    }
    SECTION("Min Hold drops the old narrower-band minimum") {
        check_change(TraceMode::MinHold, 20e6, 100e6);
    }
    SECTION("Video Average starts with the new RBW") {
        check_change(TraceMode::VideoAverage, 100e6, 20e6);
    }
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

TEST_CASE("TraceMode default is ClearWrite", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    REQUIRE(sa.traceMode() == TraceMode::ClearWrite);
    REQUIRE(sa.videoAvgCount() == 10);
}

TEST_CASE("ClearWrite returns frame unchanged (no history)", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(1e6);
    sa.setVideoBw(1e6);

    Spectrum spec;
    spec.frequencies = {0, 1e6, 2e6, 3e6, 4e6};
    spec.noise_total_W = {1e-18, 1e-18, 1e-18, 1e-18, 1e-18};
    spec.generation = 1;

    auto out1 = sa.renderSpectrum(spec);
    spec.generation = 2;
    auto out2 = sa.renderSpectrum(spec);

    REQUIRE(out1.size() == out2.size());
    for (size_t i = 0; i < out1.size(); ++i) {
        REQUIRE(out1[i] == Approx(out2[i]).epsilon(1e-9));
    }
}

TEST_CASE("MaxHold accumulates per-bin maximum", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(1e6);   // 1 MHz RBW
    sa.setVideoBw(1e6); // 1 MHz VBW (window=1 → passthrough)
    sa.setTraceMode(TraceMode::MaxHold);

    // 5 MHz spacing → RBW kernel_half=1 → smearing only reaches ±1 bin
    Spectrum spec;
    spec.frequencies = {0, 5e6, 10e6, 15e6, 20e6};
    spec.tones = {{0.0, -10.0, 0.0}}; // tone at bin 0
    spec.generation = 1;
    auto out1 = sa.renderSpectrum(spec);

    // Second frame: tone moves to bin 4 (far right)
    spec.tones = {{20e6, -10.0, 0.0}};
    spec.generation = 2;
    auto out2 = sa.renderSpectrum(spec);

    // With ClearWrite, out2[0] ≈ noise floor (no tone nearby).
    // With MaxHold, out2[0] holds the -10 dBm peak from frame 1.
    REQUIRE(out2[0] > out2[1] + 5.0); // bin 0 held high
    REQUIRE(out2[4] > out2[1] + 5.0); // bin 4 now high (tone moved)
}

TEST_CASE("MinHold accumulates per-bin minimum", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(1e6);
    sa.setVideoBw(1e6);
    sa.setTraceMode(TraceMode::MinHold);

    Spectrum spec;
    spec.frequencies = {0, 1e6, 2e6, 3e6, 4e6};

    // Frame 1: tone at bin 4 ONLY (bin 2 is noise floor, low)
    spec.tones = {{4e6, -10.0, 0.0}};
    spec.generation = 1;
    auto out1 = sa.renderSpectrum(spec);

    // Frame 2: tone at bin 2 ONLY (moved from bin 4)
    spec.tones = {{2e6, -10.0, 0.0}};
    spec.generation = 2;
    auto out2 = sa.renderSpectrum(spec);

    // MinHold: a bin that was noise in frame 1 should still be noise, even if
    // frame 2 has a tone there. ClearWrite would show the tone.
    // With RBW=1e6, noise floor << -50 dBm, so this assertion fails on ClearWrite.
    REQUIRE(out2[2] < -50.0); // bin 2 held low from frame 1 (MinHold)
    REQUIRE(out2[4] < -50.0); // bin 4 held low from frame 2
}

TEST_CASE("VideoAverage converges to constant input", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(1e6);
    sa.setVideoBw(1e6);
    sa.setTraceMode(TraceMode::VideoAverage);
    sa.setVideoAvgCount(5);

    Spectrum spec;
    spec.frequencies = {0, 1e6, 2e6, 3e6, 4e6};
    spec.noise_total_W = {1e-18, 1e-18, 1e-18, 1e-18, 1e-18};
    spec.generation = 1;

    // Feed many identical frames — EWMA should converge
    std::vector<double> prev;
    for (int i = 0; i < 50; ++i) {
        spec.generation = i + 1;
        prev = sa.renderSpectrum(spec);
    }

    // One more frame — should be essentially unchanged
    spec.generation = 51;
    auto out = sa.renderSpectrum(spec);
    for (size_t i = 0; i < out.size(); ++i) {
        REQUIRE(out[i] == Approx(prev[i]).epsilon(0.01));
    }
}

TEST_CASE("VideoAverage responds to step change", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(1e6);
    sa.setVideoBw(1e6);
    sa.setTraceMode(TraceMode::VideoAverage);
    sa.setVideoAvgCount(3);

    Spectrum spec_lo;
    spec_lo.frequencies = {0, 1e6, 2e6};
    spec_lo.noise_total_W = {1e-20, 1e-20, 1e-20};

    // Feed spec_lo for many frames to converge
    for (int i = 0; i < 100; ++i) {
        spec_lo.generation = i + 1;
        sa.renderSpectrum(spec_lo);
    }

    // Now switch to spec_hi — modify spec_lo in place because the engine
    // uses the address as the history key
    spec_lo.noise_total_W = {1e-15, 1e-15, 1e-15};
    spec_lo.generation = 101;
    auto out = sa.renderSpectrum(spec_lo);

    // After one step, output should be between old and new (EWMA blend)
    // The old converged value was ~W_to_dBm(1e-20 * 1e6) = W_to_dBm(1e-14) = -110 dBm
    // The new raw value is ~W_to_dBm(1e-15 * 1e6) = W_to_dBm(1e-9) = -60 dBm
    // With alpha = 2/(3+1) = 0.5, one step: 0.5*(-60) + 0.5*(-110) = -85 dBm
    double expected_approx = -85.0;
    for (double v : out) {
        REQUIRE(v == Approx(expected_approx).margin(5.0));
    }
}

TEST_CASE("Mode switch resets history", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(1e6);
    sa.setVideoBw(1e6);

    Spectrum spec;
    spec.frequencies = {0, 1e6, 2e6, 3e6, 4e6};
    spec.tones = {{2e6, -10.0, 0.0}};
    spec.generation = 1;

    // Build up MaxHold history
    sa.setTraceMode(TraceMode::MaxHold);
    sa.renderSpectrum(spec);

    // Switch to ClearWrite — should not see held values
    sa.setTraceMode(TraceMode::ClearWrite);
    spec.tones = {}; // remove tone
    spec.generation = 2;
    auto out = sa.renderSpectrum(spec);

    // With no tone and ClearWrite, bin 2 should be low (noise floor)
    REQUIRE(out[2] < -50.0);
}

TEST_CASE("resetTraceHistory clears all buffers", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(1e6);
    sa.setVideoBw(1e6);

    Spectrum spec;
    spec.frequencies = {0, 1e6, 2e6, 3e6, 4e6};
    spec.tones = {{2e6, -10.0, 0.0}};
    spec.generation = 1;

    sa.setTraceMode(TraceMode::MaxHold);
    sa.renderSpectrum(spec);
    sa.resetTraceHistory();

    // After reset, rendering with no tone should show noise floor (not held tone)
    spec.tones = {};
    spec.generation = 2;
    auto out = sa.renderSpectrum(spec);
    REQUIRE(out[2] < -50.0);
}

TEST_CASE("pruneHistory removes stale entries", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(1e6);
    sa.setVideoBw(1e6);
    sa.setTraceMode(TraceMode::MaxHold);

    Spectrum spec;
    spec.frequencies = {0, 1e6, 2e6};
    spec.tones = {{1e6, -10.0, 0.0}};
    spec.generation = 1;
    sa.renderSpectrum(spec);

    // Prune with empty active set — should clear all history
    sa.pruneHistory({});

    // After prune, same spec should start fresh (size mismatch resets, so no held values)
    spec.tones = {};
    spec.generation = 2;
    auto out = sa.renderSpectrum(spec);
    REQUIRE(out[1] < -50.0);
}

TEST_CASE("Size mismatch resets history", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(1e6);
    sa.setVideoBw(1e6);
    sa.setTraceMode(TraceMode::MaxHold);

    Spectrum spec;
    spec.frequencies = {0, 1e6, 2e6};
    spec.tones = {{1e6, -10.0, 0.0}};
    spec.generation = 1;
    sa.renderSpectrum(spec);

    // Change spectrum size
    spec.frequencies = {0, 1e6, 2e6, 3e6, 4e6, 5e6};
    spec.tones = {};
    spec.generation = 2;
    auto out = sa.renderSpectrum(spec);

    // All bins should be noise floor (no held values from old-size history)
    for (double v : out) {
        REQUIRE(v < -50.0);
    }
}

TEST_CASE("Amplifier nonlinear disabled = linear passthrough", "[amplifier][nonlinear]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    AmplifierEngine amp(0, graph);
    amp.setGain_dB(10.0);
    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    const auto &out = amp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].freq_Hz == 100e6);
    REQUIRE(out.tones[0].power_dBm == Approx(-10.0));
}

TEST_CASE("Amplifier generates 2nd and 3rd harmonics", "[amplifier][nonlinear]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    AmplifierEngine amp(0, graph);
    amp.setGain_dB(0.0);
    amp.setOIP2_dBm(40.0);
    amp.setOIP3_dBm(30.0);
    amp.setEnableNonlinear(true);
    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    const auto &out = amp.node().outputs[0];
    REQUIRE(out.tones.size() == 3);

    REQUIRE(out.tones[0].freq_Hz == 100e6);
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0).epsilon(0.001));

    // Expected harmonics from the corrected formulas (see nonlinear_model.h):
    //   H2 (RMS) = k1 * Vp1^2 / sqrt(2)   (v_in^2 term: peak k1*A^2/2 at 2w, A = sqrt(2)*Vp1)
    //   H3 (RMS) = k2 * Vp1^3 / 2         (v_in^3 term: peak k2*A^3/4 at 3w, A = sqrt(2)*Vp1)
    // with k1 = 1/V_oip2, k2 = 4/(3*V_oip3^2) and Vp1 the output-referred RMS voltage.
    const double Vp1 = std::sqrt(std::pow(10.0, -20.0 / 10.0) * 0.001 * 50.0);
    const double V_oip2 = std::sqrt(std::pow(10.0, 40.0 / 10.0) * 0.001 * 50.0);
    const double V_oip3 = std::sqrt(std::pow(10.0, 30.0 / 10.0) * 0.001 * 50.0);
    const double h2_v = (1.0 / V_oip2) * Vp1 * Vp1 / std::sqrt(2.0);
    const double h3_v = (4.0 / (3.0 * V_oip3 * V_oip3)) * Vp1 * Vp1 * Vp1 / 2.0;
    const double h2_expected = 10.0 * std::log10((h2_v * h2_v / 50.0) / 0.001);
    const double h3_expected = 10.0 * std::log10((h3_v * h3_v / 50.0) / 0.001);

    REQUIRE(out.tones[1].freq_Hz == 200e6);
    REQUIRE(out.tones[1].power_dBm == Approx(h2_expected).epsilon(0.01));

    REQUIRE(out.tones[2].freq_Hz == 300e6);
    REQUIRE(out.tones[2].power_dBm == Approx(h3_expected).epsilon(0.1));
}

TEST_CASE("Amplifier generates two-tone IMD products", "[amplifier][nonlinear]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.addTone(101e6, -20.0);
    gen.update(0.0);

    AmplifierEngine amp(0, graph);
    amp.setGain_dB(0.0);
    amp.setOIP2_dBm(40.0);
    amp.setOIP3_dBm(30.0);
    amp.setEnableNonlinear(true);
    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    const auto &out = amp.node().outputs[0];
    // 2 fundamentals + 4 harmonics + 6 IMD = 12 tones
    REQUIRE(out.tones.size() == 12);

    bool found_99 = false, found_102 = false;
    for (const auto &t : out.tones) {
        if (std::abs(t.freq_Hz - 99e6) < 1.0)
            found_99 = true;
        if (std::abs(t.freq_Hz - 102e6) < 1.0)
            found_102 = true;
    }
    REQUIRE(found_99);
    REQUIRE(found_102);
}

TEST_CASE("Amplifier shows compression at high input", "[amplifier][nonlinear]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, 5.0);
    gen.update(0.0);

    AmplifierEngine amp(0, graph);
    amp.setGain_dB(0.0);
    amp.setOIP3_dBm(10.0);
    amp.setOIP2_dBm(20.0);
    amp.setEnableNonlinear(true);
    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    double gain_high = amp.node().outputs[0].tones[0].power_dBm - 5.0;
    REQUIRE(gain_high < 0.0);
    REQUIRE(gain_high > -3.0);
}

TEST_CASE("Amplifier single tone produces no IMD products", "[amplifier][nonlinear]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    AmplifierEngine amp(0, graph);
    amp.setGain_dB(0.0);
    amp.setOIP2_dBm(40.0);
    amp.setOIP3_dBm(30.0);
    amp.setEnableNonlinear(true);
    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    const auto &out = amp.node().outputs[0];
    REQUIRE(out.tones.size() == 3);
}
