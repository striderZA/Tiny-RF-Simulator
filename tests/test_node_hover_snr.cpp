// App-level coverage for the node-hover SNR row.
//
// The graph widget knows nothing about the analyzer: it only hands the app a
// node id and renders whatever the app's onNodeHover callback returns. Ordinary
// components use the analyzer SNR for their first output; PFB nodes use the
// strongest tone in their active channel relative to integrated channel noise.
// Nodes without a measurable tone/noise pair report unavailable.
#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "pfb_channelizer_engine.h"
#include "splitter_engine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
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

TEST_CASE_METHOD(ImGuiFixture, "PFB node hover SNR uses integrated channel noise",
                 "[app][snr][pfb]") {
    RfSimulatorApp app;
    auto &pfb = app.testComponents().add<PFBChannelizerEngine>(10002, app.testGraphEngine());

    Spectrum input;
    input.frequencies.resize(401);
    for (int i = 0; i < 401; ++i)
        input.frequencies[i] = -200e6 + i * 1e6;
    input.noise_total_W.assign(input.frequencies.size(), 1e-20);
    input.tones.push_back({6.25e6, -20.0, 0.0});
    input.fs_Hz = 400e6;

    pfb.setActiveChannel(16);
    pfb.node().inputs[0] = &input;
    pfb.update(0.0);

    const auto &active = pfb.channels().at(pfb.activeChannel());
    REQUIRE(active.tones.size() == 1);
    REQUIRE(active.noise_W > 0.0);
    const double expected_dB =
        active.tones[0].power_dBm - (10.0 * std::log10(active.noise_W) + 30.0);

    app.testSpectrumAnalyzerEngine().setResBw(4e6);
    const NodeHoverInfo first = app.testGraphWidget().onNodeHover(pfb.graphNodeId());
    REQUIRE(first.snr_dB.has_value());
    REQUIRE(*first.snr_dB == Catch::Approx(expected_dB).margin(0.25));

    const auto analyzer_snr =
        app.testSpectrumAnalyzerEngine().computeStrongestToneSNRdB(pfb.node().outputs[0]);
    REQUIRE(analyzer_snr.has_value());
    REQUIRE(std::abs(*first.snr_dB - *analyzer_snr) > 1.0);

    app.testSpectrumAnalyzerEngine().setResBw(8e6);
    const NodeHoverInfo second = app.testGraphWidget().onNodeHover(pfb.graphNodeId());
    REQUIRE(second.snr_dB.has_value());
    REQUIRE(*second.snr_dB == Catch::Approx(*first.snr_dB).margin(1e-12));

    input.tones.clear();
    input.bumpGeneration();
    pfb.update(0.0);
    const NodeHoverInfo no_tone = app.testGraphWidget().onNodeHover(pfb.graphNodeId());
    REQUIRE_FALSE(no_tone.snr_dB.has_value());
}
