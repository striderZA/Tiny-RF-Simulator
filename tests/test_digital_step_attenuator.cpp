#include "digital_step_attenuator_engine.h"
#include "node_graph_engine.h"
#include "spectrum.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <nlohmann/json.hpp>

namespace {

Spectrum buildTestSpectrum(double freq_Hz, double power_dBm) {
    Spectrum s;
    s.frequencies = {1e9, 2e9, 3e9};
    s.tones = {{freq_Hz, power_dBm, 0.0}};
    s.noise_W.assign(3, 1e-21);
    s.noise_added_W.assign(3, 0.0);
    s.noise_total_W.assign(3, 1e-21);
    s.phase_deg.assign(3, 0.0);
    s.fs_Hz = 0.0;
    return s;
}

constexpr double TEST_K = 1.3806e-23;
constexpr double TEST_T = 290.0;

// Independent oracle for the power conversion — not the engine's helpers.
double dbToLin(double dB) { return std::pow(10.0, -dB / 10.0); }

} // namespace

// ZX76-31R5A-PN+ datasheet defaults (Mini-Circuits): 6-bit parallel control,
// 0.5 dB step, 0-31.5 dB range, 1.4 dB typ insertion loss at zero code
// (DC-1 GHz, 2.0 dB max).
TEST_CASE("Digital step attenuator: defaults match the ZX76-31R5A-PN+",
          "[digital_step_attenuator]") {
    NodeGraphEngine graph;
    DigitalStepAttenuatorEngine att(1, graph);

    CHECK(att.type_name() == std::string_view("digital_step_attenuator"));
    CHECK(att.bitDepth() == 6);
    CHECK(att.stepSize_dB() == Catch::Approx(0.5));
    CHECK(att.refInsertionLoss_dB() == Catch::Approx(1.4));
    CHECK(att.code() == 0);
    CHECK(att.maxCode() == 63);
    CHECK(att.attenuation() == Catch::Approx(1.4));
    CHECK(att.numInputPins() == 1);
    REQUIRE(att.node().outputs.size() == 1);
    CHECK(att.inputPinId() != -1);

    // The graph label must start with the registry label_prefix so the canvas
    // resolves NodeKind::DigitalStepAttenuator, not the Attenuator glyph.
    const GraphNode &node = graph.nodes()[0];
    CHECK(node.label.rfind("Digital Step Attenuator", 0) == 0);
}

TEST_CASE("Digital step attenuator: attenuation is ref loss plus code times step",
          "[digital_step_attenuator]") {
    NodeGraphEngine graph;
    DigitalStepAttenuatorEngine att(1, graph);

    att.setRefInsertionLoss(2.0);
    att.setStepSize(1.0);
    att.setCode(11);
    CHECK(att.totalAttenuation_dB() == Catch::Approx(13.0));
    CHECK(att.attenuation() == Catch::Approx(13.0));

    // Zero code keeps only the reference insertion loss.
    att.setCode(0);
    CHECK(att.totalAttenuation_dB() == Catch::Approx(2.0));
}

TEST_CASE("Digital step attenuator: tones get the total attenuation", "[digital_step_attenuator]") {
    NodeGraphEngine graph;
    DigitalStepAttenuatorEngine att(1, graph);
    att.setRefInsertionLoss(1.4);
    att.setStepSize(0.5);
    att.setCode(63); // ZX76 maximum: 31.5 dB of programmed attenuation.

    Spectrum in = buildTestSpectrum(1e9, 0.0);
    att.node().inputs[0] = &in;
    att.update(0.0);

    const Spectrum &out = att.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    CHECK(out.tones[0].freq_Hz == Catch::Approx(1e9));
    CHECK(out.tones[0].power_dBm == Catch::Approx(-32.9).margin(1e-9));
}

TEST_CASE("Digital step attenuator: passive thermal noise is kT(1-G)",
          "[digital_step_attenuator]") {
    NodeGraphEngine graph;
    DigitalStepAttenuatorEngine att(1, graph);
    att.setRefInsertionLoss(1.4);
    att.setStepSize(0.5);
    att.setCode(31);

    Spectrum in = buildTestSpectrum(1e9, -10.0);
    att.node().inputs[0] = &in;
    att.update(0.0);

    const double atten = 1.4 + 31 * 0.5;
    const double g = dbToLin(atten);
    const Spectrum &out = att.node().outputs[0];
    for (size_t i = 0; i < out.noise_total_W.size(); ++i) {
        CHECK(out.noise_W[i] == Catch::Approx(g * 1e-21).epsilon(1e-12));
        CHECK(out.noise_added_W[i] == Catch::Approx(TEST_K * TEST_T * (1.0 - g)).epsilon(1e-12));
        CHECK(out.noise_total_W[i] ==
              Catch::Approx(g * 1e-21 + TEST_K * TEST_T * (1.0 - g)).epsilon(1e-12));
    }
}

TEST_CASE("Digital step attenuator: code change recomputes on the next update",
          "[digital_step_attenuator]") {
    NodeGraphEngine graph;
    DigitalStepAttenuatorEngine att(1, graph);
    att.setCode(0);

    Spectrum in = buildTestSpectrum(1e9, 0.0);
    att.node().inputs[0] = &in;
    att.update(0.0);
    CHECK(att.node().outputs[0].tones[0].power_dBm == Catch::Approx(-1.4).margin(1e-9));

    att.setCode(4);
    att.update(0.0);
    CHECK(att.node().outputs[0].tones[0].power_dBm == Catch::Approx(-3.4).margin(1e-9));
}

TEST_CASE("Digital step attenuator: clean engine skips recomputation",
          "[digital_step_attenuator]") {
    NodeGraphEngine graph;
    DigitalStepAttenuatorEngine att(1, graph);

    Spectrum in = buildTestSpectrum(1e9, 0.0);
    att.node().inputs[0] = &in;

    att.update(0.016);
    const uint64_t gen1 = att.node().outputs[0].generation;
    att.update(0.016);
    const uint64_t gen2 = att.node().outputs[0].generation;
    REQUIRE(gen1 == gen2);
}

TEST_CASE("Digital step attenuator: code and bit depth clamp to their ranges",
          "[digital_step_attenuator]") {
    NodeGraphEngine graph;
    DigitalStepAttenuatorEngine att(1, graph);

    att.setCode(63);
    CHECK(att.code() == 63);
    att.setCode(64);
    CHECK(att.code() == 63);
    att.setCode(-5);
    CHECK(att.code() == 0);

    // Shrinking the bit depth re-clamps the stored code.
    att.setCode(40);
    att.setBitDepth(3);
    CHECK(att.bitDepth() == 3);
    CHECK(att.maxCode() == 7);
    CHECK(att.code() == 7);

    // Bit depth itself is bounded 1..16.
    att.setBitDepth(0);
    CHECK(att.bitDepth() == 1);
    att.setBitDepth(17);
    CHECK(att.bitDepth() == 16);
    CHECK(att.maxCode() == 65535);

    // Step and reference loss clamp to non-negative ranges.
    att.setStepSize(-1.0);
    CHECK(att.stepSize_dB() == Catch::Approx(0.0));
    att.setRefInsertionLoss(-2.0);
    CHECK(att.refInsertionLoss_dB() == Catch::Approx(0.0));
}

TEST_CASE("Digital step attenuator: serialize and deserialize round-trip state",
          "[digital_step_attenuator]") {
    NodeGraphEngine graph;
    DigitalStepAttenuatorEngine att(1, graph);
    att.setBitDepth(4);
    att.setStepSize(2.5);
    att.setRefInsertionLoss(0.8);
    att.setCode(9);

    const nlohmann::json state = att.serialize();
    CHECK(state.at("bit_depth").get<int>() == 4);
    CHECK(state.at("step_dB").get<double>() == Catch::Approx(2.5));
    CHECK(state.at("ref_loss_dB").get<double>() == Catch::Approx(0.8));
    CHECK(state.at("code").get<int>() == 9);

    DigitalStepAttenuatorEngine restored(2, graph);
    restored.deserialize(state);
    CHECK(restored.bitDepth() == 4);
    CHECK(restored.stepSize_dB() == Catch::Approx(2.5));
    CHECK(restored.refInsertionLoss_dB() == Catch::Approx(0.8));
    CHECK(restored.code() == 9);
    CHECK(restored.attenuation() == Catch::Approx(0.8 + 9 * 2.5));

    // Missing keys keep the engine's current state.
    restored.deserialize({{"code", 3}});
    CHECK(restored.bitDepth() == 4);
    CHECK(restored.code() == 3);

    // Out-of-range values clamp like the setters do (code 999999 exceeds the
    // 16-bit range; a value below maxCode stays as-is).
    restored.deserialize(
        {{"bit_depth", 99}, {"code", 999999}, {"step_dB", -1.0}, {"ref_loss_dB", -1.0}});
    CHECK(restored.bitDepth() == 16);
    CHECK(restored.code() == 65535);
    CHECK(restored.stepSize_dB() == Catch::Approx(0.0));
    CHECK(restored.refInsertionLoss_dB() == Catch::Approx(0.0));
}

TEST_CASE("Digital step attenuator: hover summary names the code and attenuation",
          "[digital_step_attenuator]") {
    NodeGraphEngine graph;
    DigitalStepAttenuatorEngine att(1, graph);
    att.setCode(21);
    const std::string summary(att.hoverSummary());
    CHECK(summary.find("11.9") != std::string::npos); // 1.4 + 21*0.5
    CHECK(summary.find("21") != std::string::npos);
}
