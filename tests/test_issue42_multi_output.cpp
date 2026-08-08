// Regression test for GitHub issue #42:
// "Multi-output pin routing always delivers outputs[0] (splitter OUT2 / PFB
// OUT2 unconnectable)".
//
// Root cause: NodeGraphEngine::getSourceForInput() and probedSignalNodes()
// matched a link/probe's start pin against node.output_pin_ids but discarded
// the output index, and RfSimulatorApp::rewireInputs() then always wired
// `&source->outputs[0]`. Second outputs of multi-port components (splitter
// OUT2, PFB channelizer OUT2) therefore delivered OUT1's data through the
// graph, and probing OUT2 showed OUT1's spectrum.
//
// Fix: source resolution returns (SignalNode*, output_index); rewireInputs()
// binds `&node->outputs[output_index]`; probe targets carry the output index
// into the spectrum analyzer.
#include "adc_engine.h"
#include "app.h"
#include "combiner_engine.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "pfb_channelizer_engine.h"
#include "signal_generator_engine.h"
#include "splitter_engine.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

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

// Find a graph node's pin id by owning SignalNode and port.
static int outputPinFor(NodeGraphEngine &graph, SignalNode *node, int port) {
    for (const auto &gn : graph.nodes()) {
        if (gn.signal_node == node && port >= 0 &&
            static_cast<size_t>(port) < gn.output_pin_ids.size())
            return gn.output_pin_ids[static_cast<size_t>(port)];
    }
    return -1;
}

TEST_CASE_METHOD(ImGuiFixture, "Splitter OUT2 routes to Combiner IN1 with outputs[1] (issue #42)",
                 "[app][issue42]") {
    RfSimulatorApp app;
    app.newProject();

    auto &gen = app.testComponents().add<SignalGeneratorEngine>(10001, app.testGraphEngine());
    auto &splitter = app.testComponents().add<SplitterEngine>(10002, app.testGraphEngine());
    auto &combiner = app.testComponents().add<CombinerEngine>(10003, app.testGraphEngine());

    gen.addTone(100e6, -20.0);

    app.testGraphEngine().addLink(gen.outputPinId(), splitter.inputPinId());
    // Splitter OUT2 -> Combiner IN1 (second input port)
    app.testGraphEngine().addLink(splitter.outputPinId(1), combiner.inputPinId(1));

    app.update_dsp();

    // The critical routing assertion: combiner IN1 must bind OUT2's Spectrum,
    // not OUT1's.
    REQUIRE(combiner.node().inputs[1] == &splitter.node().outputs[1]);
    REQUIRE(combiner.node().inputs[0] == nullptr);

    // Spectrum correctness: generator tone flows through splitter (-3.01 dB)
    // then combiner (-3.01 dB) into the combiner output.
    const auto &out = combiner.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE_THAT(out.tones[0].freq_Hz, WithinAbs(100e6, 1.0));
    REQUIRE_THAT(out.tones[0].power_dBm, WithinAbs(-26.02, 0.01));
}

TEST_CASE_METHOD(ImGuiFixture, "Probing Splitter OUT2 resolves output index 1 (issue #42)",
                 "[app][issue42]") {
    RfSimulatorApp app;
    app.newProject();

    auto &gen = app.testComponents().add<SignalGeneratorEngine>(10001, app.testGraphEngine());
    auto &splitter = app.testComponents().add<SplitterEngine>(10002, app.testGraphEngine());

    app.testGraphEngine().addLink(gen.outputPinId(), splitter.inputPinId());

    int out2_pin = splitter.outputPinId(1);
    REQUIRE(out2_pin >= 0);
    REQUIRE(app.testGraphEngine().addProbePin(out2_pin));

    app.update_dsp();

    auto probed = app.testGraphEngine().probedSignalNodes();
    REQUIRE(probed.size() == 1);
    REQUIRE(probed[0].node == &splitter.node());
    REQUIRE(probed[0].output_index == 1);

    // The probed node's view_enabled flag is driven by the probe source.
    REQUIRE(splitter.node().view_enabled);
}

TEST_CASE_METHOD(ImGuiFixture, "Probing PFB OUT2 resolves output index 1 (issue #42)",
                 "[app][issue42]") {
    RfSimulatorApp app;
    app.newProject();

    auto &gen = app.testComponents().add<SignalGeneratorEngine>(10001, app.testGraphEngine());
    auto &adc = app.testComponents().add<AdcEngine>(10002, app.testGraphEngine());
    auto &pfb = app.testComponents().add<PFBChannelizerEngine>(10003, app.testGraphEngine());

    app.testGraphEngine().addLink(gen.outputPinId(), adc.inputPinId());
    app.testGraphEngine().addLink(adc.outputPinId(), pfb.inputPinId());

    // PFB OUT2 = full-band output (outputs[1]).
    int out2_pin = outputPinFor(app.testGraphEngine(), &pfb.node(), 1);
    REQUIRE(out2_pin >= 0);
    REQUIRE(app.testGraphEngine().addProbePin(out2_pin));

    app.update_dsp();

    auto probed = app.testGraphEngine().probedSignalNodes();
    REQUIRE(probed.size() == 1);
    REQUIRE(probed[0].node == &pfb.node());
    REQUIRE(probed[0].output_index == 1);
    REQUIRE(pfb.node().view_enabled);
}
