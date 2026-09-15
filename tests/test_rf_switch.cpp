#include "node_graph_engine.h"
#include "rf_switch_engine.h"
#include "spectrum.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

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

constexpr double TEST_K = 1.3806e-23;
constexpr double TEST_T = 290.0;

} // namespace

TEST_CASE("SPDT switch: pin shape and labels", "[rf_switch]") {
    NodeGraphEngine graph;
    RFSwitchEngine sw(1, graph);

    REQUIRE(sw.numInputPins() == 1);
    REQUIRE(sw.numOutputPins() == 2);
    REQUIRE(sw.inputPinId(0) != -1);
    REQUIRE(sw.outputPinId(0) != -1);
    REQUIRE(sw.outputPinId(1) != -1);
    REQUIRE(sw.outputPinId(0) != sw.outputPinId(1));
    REQUIRE(sw.outputPinId(2) == -1);

    const GraphNode &node = graph.nodes()[0];
    REQUIRE(node.input_labels == std::vector<std::string>{"COM"});
    REQUIRE(node.output_labels == std::vector<std::string>{"T1", "T2"});
}

TEST_CASE("SPDT switch: active throw gets insertion loss, inactive gets isolation", "[rf_switch]") {
    NodeGraphEngine graph;
    RFSwitchEngine sw(1, graph);
    sw.setInsertionLoss_dB(0.5);
    sw.setIsolation_dB(40.0);
    sw.setActiveThrow(0);

    Spectrum in = buildTestSpectrum(1e9, -10.0, 25.0);
    sw.node().inputs[0] = &in;
    sw.update(0.016);

    REQUIRE(sw.node().outputs[0].tones.size() == 1);
    REQUIRE_THAT(sw.node().outputs[0].tones[0].power_dBm, WithinAbs(-10.5, 1e-9));
    REQUIRE_THAT(sw.node().outputs[0].tones[0].phase_deg, WithinAbs(25.0, 1e-12));
    REQUIRE_THAT(sw.node().outputs[1].tones[0].power_dBm, WithinAbs(-50.0, 1e-9));

    // Switching the pole to T2 swaps which throw carries the insertion loss.
    sw.setActiveThrow(1);
    sw.update(0.016);
    REQUIRE_THAT(sw.node().outputs[0].tones[0].power_dBm, WithinAbs(-50.0, 1e-9));
    REQUIRE_THAT(sw.node().outputs[1].tones[0].power_dBm, WithinAbs(-10.5, 1e-9));
}

TEST_CASE("SPDT switch: passive noise model per throw", "[rf_switch]") {
    NodeGraphEngine graph;
    RFSwitchEngine sw(1, graph);
    sw.setInsertionLoss_dB(0.5);
    sw.setIsolation_dB(40.0);
    sw.setActiveThrow(0);

    Spectrum in = buildTestSpectrum(1e9, -10.0, 0.0);
    sw.node().inputs[0] = &in;
    sw.update(0.016);

    const double G_il = std::pow(10.0, -0.5 / 10.0);
    const double G_iso = std::pow(10.0, -40.0 / 10.0);

    const auto &active = sw.node().outputs[0];
    REQUIRE_THAT(active.noise_W[0], WithinRel(1e-21 * G_il, 1e-9));
    REQUIRE_THAT(active.noise_added_W[0], WithinRel(TEST_K * TEST_T * (1.0 - G_il), 1e-9));
    REQUIRE_THAT(active.noise_total_W[0],
                 WithinRel(1e-21 * G_il + TEST_K * TEST_T * (1.0 - G_il), 1e-9));

    const auto &inactive = sw.node().outputs[1];
    REQUIRE_THAT(inactive.noise_W[0], WithinRel(1e-21 * G_iso, 1e-9));
    REQUIRE_THAT(inactive.noise_added_W[0], WithinRel(TEST_K * TEST_T * (1.0 - G_iso), 1e-9));
}

TEST_CASE("SPDT switch: signal metadata propagates to both throws", "[rf_switch]") {
    NodeGraphEngine graph;
    RFSwitchEngine sw(1, graph);

    Spectrum in = buildTestSpectrum(2e9, -20.0, 10.0);
    in.is_complex_baseband = true;
    in.fs_Hz = 250e6;
    in.phase_deg = {5.0, -5.0, 2.5};
    sw.node().inputs[0] = &in;
    sw.update(0.016);

    for (const auto &out : sw.node().outputs) {
        REQUIRE(out.frequencies == in.frequencies);
        REQUIRE(out.phase_deg == in.phase_deg);
        REQUIRE(out.fs_Hz == 250e6);
        REQUIRE(out.is_complex_baseband == true);
    }
}

TEST_CASE("SPDT switch: dirty flag skips unchanged input, parameter edits recompute",
          "[rf_switch]") {
    NodeGraphEngine graph;
    RFSwitchEngine sw(1, graph);

    Spectrum in = buildTestSpectrum(1e9, -10.0, 0.0);
    sw.node().inputs[0] = &in;

    sw.update(0.016);
    const uint64_t gen1 = sw.node().outputs[0].generation;
    sw.update(0.016);
    const uint64_t gen2 = sw.node().outputs[0].generation;
    REQUIRE(gen1 == gen2);

    sw.setActiveThrow(1);
    sw.update(0.016);
    REQUIRE(sw.node().outputs[0].generation != gen2);

    const uint64_t gen3 = sw.node().outputs[0].generation;
    in.generation++;
    sw.update(0.016);
    REQUIRE(sw.node().outputs[0].generation != gen3);
}

TEST_CASE("SPDT switch: setters clamp to their documented ranges", "[rf_switch]") {
    NodeGraphEngine graph;
    RFSwitchEngine sw(1, graph);

    sw.setInsertionLoss_dB(-5.0);
    REQUIRE(sw.insertionLoss_dB() == 0.0);
    sw.setInsertionLoss_dB(1000.0);
    REQUIRE(sw.insertionLoss_dB() == RFSwitchEngine::MAX_INSERTION_LOSS_DB);

    sw.setIsolation_dB(-3.0);
    REQUIRE(sw.isolation_dB() == 0.0);
    sw.setIsolation_dB(500.0);
    REQUIRE(sw.isolation_dB() == RFSwitchEngine::MAX_ISOLATION_DB);

    sw.setActiveThrow(7);
    REQUIRE(sw.activeThrow() == 1);
    sw.setActiveThrow(-1);
    REQUIRE(sw.activeThrow() == 0);
}

TEST_CASE("SPDT switch: hover summary names the active throw", "[rf_switch]") {
    NodeGraphEngine graph;
    RFSwitchEngine sw(1, graph);
    sw.setActiveThrow(1);

    const std::string summary = sw.hoverSummary();
    REQUIRE(summary.find("SPDT") != std::string::npos);
    REQUIRE(summary.find("T2") != std::string::npos);
}

TEST_CASE("SPDT switch: serialize/deserialize round-trip and defaults", "[rf_switch]") {
    NodeGraphEngine graph;
    RFSwitchEngine a(1, graph);
    a.setActiveThrow(1);
    a.setInsertionLoss_dB(1.25);
    a.setIsolation_dB(55.0);

    RFSwitchEngine b(2, graph);
    b.deserialize(a.serialize());
    REQUIRE(b.activeThrow() == 1);
    REQUIRE(b.insertionLoss_dB() == Catch::Approx(1.25));
    REQUIRE(b.isolation_dB() == Catch::Approx(55.0));

    RFSwitchEngine c(3, graph);
    c.deserialize(nlohmann::json::object());
    REQUIRE(c.activeThrow() == 0);
    REQUIRE(c.insertionLoss_dB() == Catch::Approx(RFSwitchEngine::DEFAULT_INSERTION_LOSS_DB));
    REQUIRE(c.isolation_dB() == Catch::Approx(RFSwitchEngine::DEFAULT_ISOLATION_DB));
}

TEST_CASE("SPDT switch: deserialize accepts a named throw", "[rf_switch]") {
    NodeGraphEngine graph;
    RFSwitchEngine sw(1, graph);

    // The component-library authoring form writes the throw by name.
    sw.deserialize(nlohmann::json{{"active_throw", "T2"}});
    REQUIRE(sw.activeThrow() == 1);
    sw.deserialize(nlohmann::json{{"active_throw", "T1"}});
    REQUIRE(sw.activeThrow() == 0);

    // The .rfsim project serializer writes the integer form.
    sw.deserialize(nlohmann::json{{"active_throw", 1}});
    REQUIRE(sw.activeThrow() == 1);

    // An unrecognized name leaves the current throw alone, mirroring
    // IdealFilterEngine's filter_type handling.
    sw.deserialize(nlohmann::json{{"active_throw", "T3"}});
    REQUIRE(sw.activeThrow() == 1);
}

TEST_CASE("SPDT switch: node kind has a distinct theme color", "[rf_switch]") {
    REQUIRE(themeColor(NodeKind::RFSwitchSPDT) != themeColor(NodeKind::Unknown));
    REQUIRE(themeColor(NodeKind::RFSwitchSPDT) == 0xFFE879F9u);
}
