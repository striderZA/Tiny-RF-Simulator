// App-level coverage for the node-hover SNR row.
//
// The graph widget knows nothing about the analyzer: it only hands the app a
// node id and renders whatever the app's onNodeHover callback returns. This test
// pins that contract end to end — the tooltip body keeps
// ComponentRegistry::hoverSummary(), the SNR value comes from the app's
// SpectrumAnalyzerEngine for the node's *first* output only (output 1 must not
// leak into the row), and a node whose first output has no measurable tone
// reports "unavailable" rather than a stale or fabricated number.
#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "splitter_engine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>

namespace {

struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
    }
    ~ImGuiFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

constexpr size_t kBins = 11; // 0 .. 10 MHz
constexpr double kBinWidth_Hz = 1e6;
constexpr double kToneFreq_Hz = 5e6;
constexpr double kNoiseDensity_W_per_Hz = 1e-18;
constexpr double kRbw_Hz = 4e6;

// Uniform 1 MHz grid over 0..10 MHz carrying a flat noise density and one 5 MHz
// tone — the shape the analyzer's strongest-tone measurement is defined on.
Spectrum makeOutputSpectrum(double tone_power_dBm) {
    Spectrum spec;
    spec.frequencies.resize(kBins);
    for (size_t i = 0; i < kBins; ++i)
        spec.frequencies[i] = static_cast<double>(i) * kBinWidth_Hz;
    spec.noise_total_W.assign(kBins, kNoiseDensity_W_per_Hz);
    spec.tones = {{kToneFreq_Hz, tone_power_dBm, 0.0}};
    return spec;
}

} // namespace

TEST_CASE_METHOD(ImGuiFixture, "Node hover carries the analyzer SNR of the first output",
                 "[app][snr]") {
    RfSimulatorApp app;

    auto &splitter = app.testComponents().add<SplitterEngine>(10001, app.testGraphEngine());
    REQUIRE(splitter.node().outputs.size() == 2);

    // Output 0 is the stronger of the two so that reading the wrong port is
    // visible as a ~20 dB error, not a rounding difference.
    splitter.node().outputs[0] = makeOutputSpectrum(-20.0);
    splitter.node().outputs[1] = makeOutputSpectrum(-40.0);

    app.testSpectrumAnalyzerEngine().setResBw(kRbw_Hz);

    REQUIRE(app.testGraphWidget().onNodeHover);

    const int node_id = splitter.graphNodeId();
    const NodeHoverInfo info = app.testGraphWidget().onNodeHover(node_id);

    // The registry summary still forms the tooltip body.
    REQUIRE_FALSE(info.summary.empty());
    REQUIRE(info.summary == app.testComponents().hoverSummary(node_id));

    // The reported SNR is exactly what the analyzer API measures for output 0.
    const std::optional<double> expected =
        app.testSpectrumAnalyzerEngine().computeStrongestToneSNRdB(splitter.node().outputs[0]);
    REQUIRE(expected.has_value());
    REQUIRE(info.snr_dB.has_value());
    REQUIRE(*info.snr_dB == Catch::Approx(*expected).margin(0.25));

    // -20 dBm (1e-5 W) against a 4 MHz-wide slice of 1e-18 W/Hz (4e-12 W).
    REQUIRE(*info.snr_dB == Catch::Approx(64.0).margin(0.25));

    // Output 1 measured instead would read ~20 dB lower: pin the first-output choice.
    const std::optional<double> weaker =
        app.testSpectrumAnalyzerEngine().computeStrongestToneSNRdB(splitter.node().outputs[1]);
    REQUIRE(weaker.has_value());
    REQUIRE(*info.snr_dB - *weaker == Catch::Approx(20.0).margin(0.25));

    // With no tone left on the first output the summary survives and the SNR row
    // falls back to its unavailable form.
    splitter.node().outputs[0].tones.clear();
    const NodeHoverInfo no_tone = app.testGraphWidget().onNodeHover(node_id);
    REQUIRE_FALSE(no_tone.summary.empty());
    REQUIRE_FALSE(no_tone.snr_dB.has_value());
}
