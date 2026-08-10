#include "amplifier_engine.h"
#include "attenuator_engine.h"
#include "mixer_engine.h"
#include "network_analyzer_engine.h"
#include "node_graph_engine.h"
#include "spectrum.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

TEST_CASE("NetworkAnalyzer: stimulus comb evenly spaced", "[network_analyzer]") {
    NodeGraphEngine graph;
    NetworkAnalyzerEngine na(1, graph);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(11);
    na.setStimulusPower(-20.0);

    na.update(0.016);

    const auto &out = na.node().outputs[0];
    REQUIRE(out.tones.size() == 11);
    REQUIRE(out.frequencies.size() == 11);
    REQUIRE_THAT(out.tones[0].freq_Hz, WithinAbs(1e9, 1.0));
    REQUIRE_THAT(out.tones[10].freq_Hz, WithinAbs(2e9, 1.0));
    REQUIRE_THAT(out.tones[5].freq_Hz, WithinAbs(1.5e9, 1.0));
    for (const auto &t : out.tones)
        REQUIRE(t.power_dBm == -20.0);
    REQUIRE(na.sweepFrequencies().size() == 11);
}

TEST_CASE("NetworkAnalyzer: gain accuracy against attenuator", "[network_analyzer]") {
    NodeGraphEngine graph;
    NetworkAnalyzerEngine na(1, graph);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(21);
    na.setStimulusPower(-20.0);
    na.update(0.016);

    AttenuatorEngine atten(2, graph);
    atten.setAttenuation(10.0);
    atten.node().inputs[0] = &na.node().outputs[0];
    atten.update(0.016);

    na.node().inputs[0] = &atten.node().outputs[0];
    na.update(0.016);

    REQUIRE(na.gainDb().size() == 21);
    for (double g : na.gainDb())
        REQUIRE_THAT(g, WithinAbs(-10.0, 0.05));
}

TEST_CASE("NetworkAnalyzer: noise figure accuracy against amplifier", "[network_analyzer]") {
    NodeGraphEngine graph;
    NetworkAnalyzerEngine na(1, graph);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(21);
    na.setStimulusPower(-20.0);
    na.update(0.016);

    AmplifierEngine amp(2, graph);
    amp.setGain_dB(20.0);
    amp.setNF_dB(5.0);
    amp.node().inputs[0] = &na.node().outputs[0];
    amp.update(0.016);

    na.node().inputs[0] = &amp.node().outputs[0];
    na.update(0.016);

    REQUIRE(na.noiseFigureDb().size() == 21);
    for (double nf : na.noiseFigureDb())
        REQUIRE_THAT(nf, WithinAbs(5.0, 0.1));
    for (double g : na.gainDb())
        REQUIRE_THAT(g, WithinAbs(20.0, 0.05));
}

TEST_CASE("NetworkAnalyzer: non-invasive tap does not perturb real consumer",
          "[network_analyzer]") {
    NodeGraphEngine graph;
    NetworkAnalyzerEngine na(1, graph);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(11);
    na.update(0.016);

    // Baseline: attenuator reads the NA's stimulus with no tap present.
    AttenuatorEngine baseline(2, graph);
    baseline.setAttenuation(3.0);
    baseline.node().inputs[0] = &na.node().outputs[0];
    baseline.update(0.016);
    std::vector<double> baseline_powers;
    for (const auto &t : baseline.node().outputs[0].tones)
        baseline_powers.push_back(t.power_dBm);

    // Same scenario, but a second NetworkAnalyzer also taps the exact same
    // source Spectrum* as its Port 2 (ordinary multi-fanout, per
    // NodeGraphEngine's connection_multi_fanout test). Must not change what
    // the real consumer (the attenuator) sees.
    AttenuatorEngine tapped(3, graph);
    tapped.setAttenuation(3.0);
    tapped.node().inputs[0] = &na.node().outputs[0];

    NetworkAnalyzerEngine tap(4, graph);
    tap.node().inputs[0] = &na.node().outputs[0];
    tap.update(0.016);

    tapped.update(0.016);

    REQUIRE(tapped.node().outputs[0].tones.size() == baseline_powers.size());
    for (size_t i = 0; i < baseline_powers.size(); ++i)
        REQUIRE(tapped.node().outputs[0].tones[i].power_dBm == baseline_powers[i]);
}

TEST_CASE("NetworkAnalyzer: dirty-flag caching skips redundant recompute", "[network_analyzer]") {
    NodeGraphEngine graph;
    NetworkAnalyzerEngine na(1, graph);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(11);
    na.update(0.016);

    AttenuatorEngine atten(2, graph);
    atten.setAttenuation(3.0);
    atten.node().inputs[0] = &na.node().outputs[0];
    atten.update(0.016);
    na.node().inputs[0] = &atten.node().outputs[0];

    na.update(0.016);
    int count1 = na.measurementRecomputeCount();
    REQUIRE(count1 >= 1);

    // Nothing changed: recompute must be skipped.
    na.update(0.016);
    REQUIRE(na.measurementRecomputeCount() == count1);
    na.update(0.016);
    REQUIRE(na.measurementRecomputeCount() == count1);

    // Upstream generation changes: recompute must happen again.
    atten.setAttenuation(6.0);
    atten.update(0.016);
    na.update(0.016);
    REQUIRE(na.measurementRecomputeCount() == count1 + 1);
}

TEST_CASE("NetworkAnalyzer: mixer frequency translation reports no-data", "[network_analyzer]") {
    NodeGraphEngine graph;
    NetworkAnalyzerEngine na(1, graph);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(11);
    na.update(0.016);

    MixerEngine mixer(2, graph);
    mixer.setLoFreq_Hz(1.23456e9); // deliberately grid-misaligned
    mixer.setConversionGain_dB(0.0);
    mixer.node().inputs[0] = &na.node().outputs[0];
    mixer.update(0.016);

    na.node().inputs[0] = &mixer.node().outputs[0];
    na.update(0.016);

    REQUIRE(na.gainDb().size() == 11);
    for (double g : na.gainDb())
        REQUIRE(std::isnan(g));
    for (double nf : na.noiseFigureDb())
        REQUIRE(std::isnan(nf));
}

TEST_CASE("NetworkAnalyzer: serialization round-trip", "[network_analyzer]") {
    NodeGraphEngine graph;
    NetworkAnalyzerEngine na(1, graph);
    na.setStartFrequency(2.4e9);
    na.setStopFrequency(2.5e9);
    na.setPoints(51);
    na.setStimulusPower(-15.0);

    auto j = na.serialize();

    NetworkAnalyzerEngine restored(2, graph);
    restored.deserialize(j);

    REQUIRE(restored.startFrequency() == 2.4e9);
    REQUIRE(restored.stopFrequency() == 2.5e9);
    REQUIRE(restored.points() == 51);
    REQUIRE(restored.stimulusPower() == -15.0);
}

TEST_CASE("NetworkAnalyzer: points clamped to [2, 2001]", "[network_analyzer]") {
    NodeGraphEngine graph;
    NetworkAnalyzerEngine na(1, graph);

    na.setPoints(1);
    REQUIRE(na.points() == 2);

    na.setPoints(5000);
    REQUIRE(na.points() == 2001);

    na.setPoints(201);
    REQUIRE(na.points() == 201);
}

TEST_CASE("NetworkAnalyzer: hover summary", "[network_analyzer]") {
    NodeGraphEngine graph;
    NetworkAnalyzerEngine na(1, graph);
    na.setStartFrequency(1e9);
    na.setStopFrequency(6e9);

    std::string summary = na.hoverSummary();
    REQUIRE(summary.find("Network Analyzer") != std::string::npos);
}
