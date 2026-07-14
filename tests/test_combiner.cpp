#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "combiner_engine.h"
#include "node_graph_engine.h"
#include "spectrum.h"
#include <cmath>

using Catch::Matchers::WithinAbs;

namespace {

Spectrum buildTestSpectrum(double freq_Hz, double power_dBm, double phase_deg) {
    Spectrum s;
    s.frequencies = {1e9, 2e9, 3e9};
    s.tones = {{freq_Hz, power_dBm, phase_deg}};
    s.noise_W.assign(3, 1e-21);
    s.noise_added_W.assign(3, 0.0);
    s.noise_total_W.assign(3, 1e-21);
    s.phase_deg.assign(3, 0.0);
    s.fs_Hz = 1e9;
    return s;
}

} // namespace

TEST_CASE("Combiner: basic combination with -3 dB loss per input", "[combiner]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);

    Spectrum input0 = buildTestSpectrum(1e9, -10.0, 0.0);
    Spectrum input1 = buildTestSpectrum(2e9, -20.0, 0.0);
    combiner.node().inputs[0] = &input0;
    combiner.node().inputs[1] = &input1;

    combiner.update(0.016);

    const auto& output = combiner.node().outputs[0];
    REQUIRE(output.tones.size() == 2);
    
    // Each tone should have -3 dB loss applied
    REQUIRE_THAT(output.tones[0].power_dBm, WithinAbs(-13.01, 0.01));
    REQUIRE_THAT(output.tones[1].power_dBm, WithinAbs(-23.01, 0.01));
}

TEST_CASE("Combiner: single input connected", "[combiner]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);

    Spectrum input0 = buildTestSpectrum(1e9, -10.0, 0.0);
    combiner.node().inputs[0] = &input0;
    combiner.node().inputs[1] = nullptr;

    combiner.update(0.016);

    const auto& output = combiner.node().outputs[0];
    REQUIRE(output.tones.size() == 1);
    REQUIRE_THAT(output.tones[0].power_dBm, WithinAbs(-13.01, 0.01));
}

TEST_CASE("Combiner: both inputs unconnected", "[combiner]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);

    combiner.node().inputs[0] = nullptr;
    combiner.node().inputs[1] = nullptr;

    combiner.update(0.016);

    const auto& output = combiner.node().outputs[0];
    REQUIRE(output.tones.empty());
}

TEST_CASE("Combiner: dirty-flag skip", "[combiner]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);

    Spectrum input0 = buildTestSpectrum(1e9, -10.0, 0.0);
    combiner.node().inputs[0] = &input0;

    combiner.update(0.016);
    uint64_t gen1 = combiner.node().outputs[0].generation;

    combiner.update(0.016);
    uint64_t gen2 = combiner.node().outputs[0].generation;

    REQUIRE(gen1 == gen2);
}

TEST_CASE("Combiner: hover summary", "[combiner]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);

    std::string summary = combiner.hoverSummary();
    REQUIRE(summary.find("Combiner") != std::string::npos);
    REQUIRE(summary.find("2") != std::string::npos);
    REQUIRE(summary.find("1") != std::string::npos);
}
