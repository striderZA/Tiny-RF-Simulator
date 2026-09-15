#include "node_graph_engine.h"
#include "rf_switch_2to1_engine.h"
#include "spectrum.h"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

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

// Independent oracle for expected power conversion — not the engine's dbToLinear().
double dbToLin(double dB) { return std::pow(10.0, -dB / 10.0); }

} // namespace

TEST_CASE("SPDT 2:1 switch: pin shape and labels", "[rf_switch_2to1]") {
    NodeGraphEngine graph;
    RFSwitch2to1Engine sw(1, graph);

    REQUIRE(sw.numInputPins() == 2);
    REQUIRE(sw.node().outputs.size() == 1);
    REQUIRE(sw.inputPinId(0) != -1);
    REQUIRE(sw.inputPinId(1) != -1);
    REQUIRE(sw.inputPinId(0) != sw.inputPinId(1));
    REQUIRE(sw.inputPinId(2) == -1);
    REQUIRE(sw.outputPinId(0) != -1);
    REQUIRE(sw.outputPinId(1) == -1);

    const GraphNode &node = graph.nodes()[0];
    REQUIRE(node.input_labels == std::vector<std::string>{"T1", "T2"});
    REQUIRE(node.output_labels == std::vector<std::string>{"COM"});
}

TEST_CASE("SPDT 2:1 switch: selected throw gets insertion loss, unselected gets isolation",
          "[rf_switch_2to1]") {
    NodeGraphEngine graph;
    RFSwitch2to1Engine sw(1, graph);
    sw.setActiveThrow(0);
    sw.setInsertionLoss_dB(0.5);
    sw.setIsolation_dB(40.0);

    Spectrum t1 = buildTestSpectrum(1e9, 0.0, 0.0);
    Spectrum t2 = buildTestSpectrum(2e9, 0.0, 0.0);
    sw.node().inputs[0] = &t1;
    sw.node().inputs[1] = &t2;

    sw.update(0.0);

    const Spectrum &out = sw.node().outputs[0];
    REQUIRE(out.tones.size() == 2);
    // Selected throw first, at insertion loss.
    REQUIRE(out.tones[0].freq_Hz == Catch::Approx(1e9));
    REQUIRE(out.tones[0].power_dBm == Catch::Approx(-0.5).margin(1e-9));
    // Unselected throw second, at the isolation floor.
    REQUIRE(out.tones[1].freq_Hz == Catch::Approx(2e9));
    REQUIRE(out.tones[1].power_dBm == Catch::Approx(-40.0).margin(1e-9));
}

TEST_CASE("SPDT 2:1 switch: active_throw swaps which input is the loss path", "[rf_switch_2to1]") {
    NodeGraphEngine graph;
    RFSwitch2to1Engine sw(1, graph);
    sw.setActiveThrow(1);
    sw.setInsertionLoss_dB(1.0);
    sw.setIsolation_dB(60.0);

    Spectrum t1 = buildTestSpectrum(1e9, 0.0, 0.0);
    Spectrum t2 = buildTestSpectrum(2e9, 0.0, 0.0);
    sw.node().inputs[0] = &t1;
    sw.node().inputs[1] = &t2;

    sw.update(0.0);

    const Spectrum &out = sw.node().outputs[0];
    REQUIRE(out.tones.size() == 2);
    // T2 is the selected throw now, so it is emitted first at IL.
    REQUIRE(out.tones[0].freq_Hz == Catch::Approx(2e9));
    REQUIRE(out.tones[0].power_dBm == Catch::Approx(-1.0).margin(1e-9));
    REQUIRE(out.tones[1].freq_Hz == Catch::Approx(1e9));
    REQUIRE(out.tones[1].power_dBm == Catch::Approx(-60.0).margin(1e-9));
}

TEST_CASE("SPDT 2:1 switch: noise is the superposition of both throw paths", "[rf_switch_2to1]") {
    NodeGraphEngine graph;
    RFSwitch2to1Engine sw(1, graph);
    sw.setActiveThrow(0);
    sw.setInsertionLoss_dB(0.5);
    sw.setIsolation_dB(40.0);

    Spectrum t1 = buildTestSpectrum(1e9, -10.0, 0.0);
    Spectrum t2 = buildTestSpectrum(2e9, -10.0, 0.0);
    t1.noise_total_W.assign(3, 2e-21);
    t2.noise_total_W.assign(3, 5e-21);
    sw.node().inputs[0] = &t1;
    sw.node().inputs[1] = &t2;

    sw.update(0.0);

    const double g_il = dbToLin(0.5);
    const double g_iso = dbToLin(40.0);
    const Spectrum &out = sw.node().outputs[0];
    for (size_t i = 0; i < out.noise_total_W.size(); ++i) {
        const double expected =
            g_il * 2e-21 + g_iso * 5e-21 + TEST_K * TEST_T * std::max(0.0, 1.0 - g_il - g_iso);
        REQUIRE(out.noise_total_W[i] == Catch::Approx(expected).epsilon(1e-12));
        REQUIRE(out.noise_added_W[i] ==
                Catch::Approx(TEST_K * TEST_T * std::max(0.0, 1.0 - g_il - g_iso)).epsilon(1e-12));
    }
}

TEST_CASE("SPDT 2:1 switch: noise_added never goes negative when gains sum above one",
          "[rf_switch_2to1]") {
    NodeGraphEngine graph;
    RFSwitch2to1Engine sw(1, graph);
    // IL 0 dB and ISO 3 dB give G_IL + G_ISO > 1, so an unclamped
    // 1 - G_IL - G_ISO term would drive added noise negative.
    sw.setInsertionLoss_dB(0.0);
    sw.setIsolation_dB(3.0);

    Spectrum t1 = buildTestSpectrum(1e9, 0.0, 0.0);
    Spectrum t2 = buildTestSpectrum(2e9, 0.0, 0.0);
    sw.node().inputs[0] = &t1;
    sw.node().inputs[1] = &t2;

    sw.update(0.0);

    const Spectrum &out = sw.node().outputs[0];
    for (double added : out.noise_added_W)
        REQUIRE(added >= 0.0);
}

TEST_CASE("SPDT 2:1 switch: phase_deg comes from the selected input", "[rf_switch_2to1]") {
    NodeGraphEngine graph;
    RFSwitch2to1Engine sw(1, graph);
    sw.setActiveThrow(1);

    Spectrum t1 = buildTestSpectrum(1e9, 0.0, 30.0);
    Spectrum t2 = buildTestSpectrum(2e9, 0.0, -12.5);
    t1.phase_deg = {1.0, 2.0, 3.0};
    t2.phase_deg = {7.0, 8.0, 9.0};
    sw.node().inputs[0] = &t1;
    sw.node().inputs[1] = &t2;

    sw.update(0.0);

    REQUIRE(sw.node().outputs[0].phase_deg == std::vector<double>{7.0, 8.0, 9.0});
}

TEST_CASE("SPDT 2:1 switch: metadata follows the selected input", "[rf_switch_2to1]") {
    NodeGraphEngine graph;
    RFSwitch2to1Engine sw(1, graph);
    sw.setActiveThrow(0);

    // Selected input (throw 0): lower fs, no complex baseband.
    Spectrum t1 = buildTestSpectrum(1e9, 0.0, 0.0);
    t1.fs_Hz = 2.5e9;
    t1.is_complex_baseband = false;
    sw.node().inputs[0] = &t1;

    // Unselected input (throw 1): higher fs, complex baseband.
    Spectrum t2 = buildTestSpectrum(2e9, 0.0, 0.0);
    t2.frequencies = {4e9, 5e9, 6e9};
    t2.fs_Hz = 5e9;
    t2.is_complex_baseband = true;
    sw.node().inputs[1] = &t2;

    sw.update(0.0);

    const Spectrum &out = sw.node().outputs[0];
    // frequencies and fs_Hz come from the selected input (throw 0).
    REQUIRE(out.frequencies == std::vector<double>{1e9, 2e9, 3e9});
    REQUIRE(out.fs_Hz == Catch::Approx(2.5e9));
    // is_complex_baseband is the OR of both inputs — true because the
    // unselected input has it set.
    REQUIRE(out.is_complex_baseband == true);
}

TEST_CASE("SPDT 2:1 switch: clamping", "[rf_switch_2to1]") {
    NodeGraphEngine graph;
    RFSwitch2to1Engine sw(1, graph);

    sw.setActiveThrow(-3);
    REQUIRE(sw.activeThrow() == 0);
    sw.setActiveThrow(9);
    REQUIRE(sw.activeThrow() == 1);

    sw.setInsertionLoss_dB(-5.0);
    REQUIRE(sw.insertionLoss_dB() == Catch::Approx(0.0));
    sw.setInsertionLoss_dB(999.0);
    REQUIRE(sw.insertionLoss_dB() == Catch::Approx(60.0));

    sw.setIsolation_dB(-1.0);
    REQUIRE(sw.isolation_dB() == Catch::Approx(0.0));
    sw.setIsolation_dB(999.0);
    REQUIRE(sw.isolation_dB() == Catch::Approx(120.0));
}

TEST_CASE("SPDT 2:1 switch: serialize/deserialize round-trip", "[rf_switch_2to1]") {
    NodeGraphEngine graph;
    RFSwitch2to1Engine a(1, graph);
    a.setActiveThrow(1);
    a.setInsertionLoss_dB(2.25);
    a.setIsolation_dB(55.0);

    NodeGraphEngine graph2;
    RFSwitch2to1Engine b(2, graph2);
    b.deserialize(a.serialize());

    REQUIRE(b.activeThrow() == 1);
    REQUIRE(b.insertionLoss_dB() == Catch::Approx(2.25));
    REQUIRE(b.isolation_dB() == Catch::Approx(55.0));

    // Partial JSON falls back to defaults.
    NodeGraphEngine graph3;
    RFSwitch2to1Engine c(3, graph3);
    c.deserialize(nlohmann::json::object());
    REQUIRE(c.activeThrow() == 0);
    REQUIRE(c.insertionLoss_dB() == Catch::Approx(0.5));
    REQUIRE(c.isolation_dB() == Catch::Approx(40.0));
}

TEST_CASE("SPDT 2:1 switch: deserialize accepts a named throw", "[rf_switch_2to1]") {
    NodeGraphEngine graph;
    RFSwitch2to1Engine sw(1, graph);

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

TEST_CASE("SPDT 2:1 switch: an unchanged input generation recomputes nothing", "[rf_switch_2to1]") {
    NodeGraphEngine graph;
    RFSwitch2to1Engine sw(1, graph);
    sw.setInsertionLoss_dB(3.0);

    Spectrum t1 = buildTestSpectrum(1e9, 0.0, 0.0);
    Spectrum t2 = buildTestSpectrum(2e9, 0.0, 0.0);
    sw.node().inputs[0] = &t1;
    sw.node().inputs[1] = &t2;

    sw.update(0.0);
    const uint64_t first = sw.node().outputs[0].generation;

    sw.update(0.0);
    REQUIRE(sw.node().outputs[0].generation == first);

    // Bump input 0 — output should recompute.
    t1.bumpGeneration();
    sw.update(0.0);
    const uint64_t after_in0 = sw.node().outputs[0].generation;
    REQUIRE(after_in0 != first);

    // Bump input 1 — output should also recompute.
    t2.bumpGeneration();
    sw.update(0.0);
    const uint64_t after_in1 = sw.node().outputs[0].generation;
    REQUIRE(after_in1 != after_in0);

    // A parameter edit alone — no input pointer or generation change at all —
    // must also force a recompute: this is the only route by which the
    // inspector's Active Throw / Insertion Loss / Isolation edits reach the
    // DSP (setters set m_dirty; beginUpdate2's m_dirty term is what carries
    // it). Without that term every setter edit is silently dropped.
    sw.setActiveThrow(1);
    sw.update(0.0);
    REQUIRE(sw.node().outputs[0].generation != after_in1);
}
