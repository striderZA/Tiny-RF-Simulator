// Regression test for GitHub issue #78:
// "Preserve multi-output links and probes across project reloads."
//
// Splitter and PFB channelizer engines construct two outputs internally
// (ComponentEngineBase ctor passes num_outputs=2) but did not report them
// through the public component API: numOutputPins() kept the interface
// default of 1, and the PFB never overrode the indexed outputPinId(int) (the
// interface default returns -1 for any port other than 0). Project save/load
// restores links and probes by resolving the saved {component, port} pairs
// back through comp->outputPinId(port), so a PFB OUT2 link or probe resolved
// to -1 and was silently dropped on reload.
#include "adc_engine.h"
#include "amplifier_engine.h"
#include "app.h"
#include "combiner_engine.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "node_graph_engine.h"
#include "pfb_channelizer_engine.h"
#include "signal_generator_engine.h"
#include "splitter_engine.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <string>

struct Issue78ImGuiFixture {
    Issue78ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
    }
    ~Issue78ImGuiFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

// Unique temp filename per call so parallel test processes don't collide.
static int s_temp_counter = 0;
static std::string tempPath() {
    return "test_issue78_roundtrip_" + std::to_string(s_temp_counter++) + ".rfsim";
}

template <typename T> static T *findOne(ComponentRegistry &registry) {
    auto found = registry.byType<T>();
    return found.empty() ? nullptr : found.front();
}

TEST_CASE_METHOD(Issue78ImGuiFixture,
                 "Splitter and PFB report two outputs and resolve both output pin ids (issue #78)",
                 "[app][issue78]") {
    NodeGraphEngine graph;
    SplitterEngine splitter(9001, graph);
    PFBChannelizerEngine pfb(9002, graph);

    // numOutputPins() must report the actual output count, not the
    // interface default of one.
    REQUIRE(splitter.numInputPins() == 1);
    REQUIRE(splitter.numOutputPins() == 2);
    REQUIRE(pfb.numInputPins() == 1);
    REQUIRE(pfb.numOutputPins() == 2);

    // Indexed outputPinId() resolves every output the engine owns; the
    // no-arg accessor stays the port-0 pin.
    REQUIRE(splitter.outputPinId(0) >= 0);
    REQUIRE(splitter.outputPinId(1) >= 0);
    REQUIRE(splitter.outputPinId(0) != splitter.outputPinId(1));
    REQUIRE(splitter.outputPinId(2) == -1);
    REQUIRE(splitter.outputPinId() == splitter.outputPinId(0));

    REQUIRE(pfb.outputPinId(0) >= 0);
    REQUIRE(pfb.outputPinId(1) >= 0);
    REQUIRE(pfb.outputPinId(0) != pfb.outputPinId(1));
    REQUIRE(pfb.outputPinId(2) == -1);
    REQUIRE(pfb.outputPinId() == pfb.outputPinId(0));
}

TEST_CASE_METHOD(Issue78ImGuiFixture,
                 "Round-trip: splitter OUT2 link and probe survive save/load (issue #78)",
                 "[app][project][issue78]") {
    const std::string path = tempPath();
    std::remove(path.c_str());
    {
        RfSimulatorApp app;
        app.newProject();

        auto &gen = app.testComponents().add<SignalGeneratorEngine>(10101, app.testGraphEngine());
        gen.addTone(100e6, -20.0);
        auto &splitter = app.testComponents().add<SplitterEngine>(10102, app.testGraphEngine());
        auto &combiner = app.testComponents().add<CombinerEngine>(10103, app.testGraphEngine());

        app.testGraphEngine().addLink(gen.outputPinId(), splitter.inputPinId());
        app.testGraphEngine().addLink(splitter.outputPinId(0), combiner.inputPinId(0));
        // The OUT2 link (output port 1) is the behavior under test.
        app.testGraphEngine().addLink(splitter.outputPinId(1), combiner.inputPinId(1));

        REQUIRE(splitter.numOutputPins() == 2);
        REQUIRE(app.testGraphEngine().addProbePin(splitter.outputPinId(1)));

        app.saveProject(path);
    }
    {
        RfSimulatorApp app;
        app.loadProject(path);
        REQUIRE(app.componentCount() == 3);
        REQUIRE(app.testGraphEngine().links().size() == 3);
        REQUIRE(app.testGraphEngine().probePins().size() == 1);

        auto *splitter = findOne<SplitterEngine>(app.testComponents());
        auto *combiner = findOne<CombinerEngine>(app.testComponents());
        REQUIRE(splitter != nullptr);
        REQUIRE(combiner != nullptr);
        REQUIRE(splitter->numOutputPins() == 2);

        app.update_dsp();

        // Both combiner inputs rebind to the splitter's own outputs — the
        // OUT2 link must deliver outputs[1], not outputs[0].
        REQUIRE(combiner->node().inputs[0] == &splitter->node().outputs[0]);
        REQUIRE(combiner->node().inputs[1] == &splitter->node().outputs[1]);

        // The restored probe targets OUT2 (output index 1).
        auto probed = app.testGraphEngine().probedSignalNodes();
        REQUIRE(probed.size() == 1);
        REQUIRE(probed[0].node == &splitter->node());
        REQUIRE(probed[0].output_index == 1);
    }
    std::remove(path.c_str());
}

TEST_CASE_METHOD(Issue78ImGuiFixture,
                 "Round-trip: PFB OUT2 link and probe survive save/load (issue #78)",
                 "[app][project][issue78]") {
    const std::string path = tempPath();
    std::remove(path.c_str());
    {
        RfSimulatorApp app;
        app.newProject();

        auto &gen = app.testComponents().add<SignalGeneratorEngine>(10201, app.testGraphEngine());
        gen.addTone(100e6, -20.0);
        auto &adc = app.testComponents().add<AdcEngine>(10202, app.testGraphEngine());
        auto &pfb = app.testComponents().add<PFBChannelizerEngine>(10203, app.testGraphEngine());
        auto &amp = app.testComponents().add<AmplifierEngine>(10204, app.testGraphEngine());

        app.testGraphEngine().addLink(gen.outputPinId(), adc.inputPinId());
        app.testGraphEngine().addLink(adc.outputPinId(), pfb.inputPinId());
        // The OUT2 link (output port 1) is the behavior under test.
        app.testGraphEngine().addLink(pfb.outputPinId(1), amp.inputPinId());

        REQUIRE(pfb.numOutputPins() == 2);
        REQUIRE(app.testGraphEngine().addProbePin(pfb.outputPinId(1)));

        app.saveProject(path);
    }
    {
        RfSimulatorApp app;
        app.loadProject(path);
        REQUIRE(app.componentCount() == 4);
        REQUIRE(app.testGraphEngine().links().size() == 3);
        REQUIRE(app.testGraphEngine().probePins().size() == 1);

        auto *pfb = findOne<PFBChannelizerEngine>(app.testComponents());
        auto *amp = findOne<AmplifierEngine>(app.testComponents());
        REQUIRE(pfb != nullptr);
        REQUIRE(amp != nullptr);
        REQUIRE(pfb->numOutputPins() == 2);

        app.update_dsp();

        // The amplifier input must rebind to the PFB's OUT2 Spectrum
        // (outputs[1]), not silently fall back to outputs[0].
        REQUIRE(amp->node().inputs[0] == &pfb->node().outputs[1]);

        // The restored probe targets OUT2 (output index 1).
        auto probed = app.testGraphEngine().probedSignalNodes();
        REQUIRE(probed.size() == 1);
        REQUIRE(probed[0].node == &pfb->node());
        REQUIRE(probed[0].output_index == 1);
    }
    std::remove(path.c_str());
}
