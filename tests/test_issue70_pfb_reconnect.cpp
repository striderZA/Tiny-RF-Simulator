// Regression test for GitHub issue #70:
// "PFB Channelizer input inconsistency"
//
// The PFB accepts only an RF ADC output. Removing the ADC must reject the
// original generator-to-PFB reconnect instead of retaining stale ADC state.
#include "adc_engine.h"
#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "pfb_channelizer_engine.h"
#include "signal_generator_engine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <fstream>
#include <string>

struct Issue70ImGuiFixture {
    Issue70ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
    }
    ~Issue70ImGuiFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

TEST_CASE_METHOD(Issue70ImGuiFixture, "PFB enforces ADC-only input across reconnects (issue #70)",
                 "[app][pfb][regression][issue70]") {
    RfSimulatorApp app;
    app.newProject();

    auto &gen = app.testComponents().add<SignalGeneratorEngine>(10001, app.testGraphEngine());
    gen.addTone(100e6, -20.0);
    auto &adc = app.testComponents().add<AdcEngine>(10002, app.testGraphEngine());
    auto &pfb = app.testComponents().add<PFBChannelizerEngine>(10003, app.testGraphEngine());

    REQUIRE(app.testGraphWidget().onLinkCreating);
    REQUIRE_FALSE(app.testGraphWidget().onLinkCreating(gen.outputPinId(), pfb.inputPinId()));
    app.update_dsp();
    REQUIRE(pfb.node().inputs[0] == nullptr);
    REQUIRE(pfb.fs_Hz() == 0.0);
    REQUIRE(pfb.node().outputs[0].frequencies.empty());

    app.testGraphEngine().addLink(gen.outputPinId(), adc.inputPinId());
    app.testGraphEngine().addLink(adc.outputPinId(), pfb.inputPinId());
    app.update_dsp();
    REQUIRE(pfb.fs_Hz() == Catch::Approx(500e6));
    REQUIRE_FALSE(pfb.node().outputs[0].frequencies.empty());

    REQUIRE(app.testGraphWidget().onRemoveNode);
    app.testGraphWidget().onRemoveNode(adc.graphNodeId());
    app.update_dsp();
    REQUIRE(pfb.node().inputs[0] == nullptr);

    REQUIRE_FALSE(app.testGraphWidget().onLinkCreating(gen.outputPinId(), pfb.inputPinId()));
    app.update_dsp();
    REQUIRE(pfb.node().inputs[0] == nullptr);
    REQUIRE(pfb.fs_Hz() == 0.0);
    REQUIRE(pfb.node().outputs[0].frequencies.empty());
    REQUIRE(pfb.node().outputs[1].frequencies.empty());
}

TEST_CASE_METHOD(Issue70ImGuiFixture, "Project load rejects direct PFB input links (issue #70)",
                 "[app][pfb][regression][issue70][project]") {
    const std::string path = "test_issue70_invalid_link.rfsim";
    std::remove(path.c_str());
    {
        std::ofstream out(path);
        out << R"json({
            "version": 1,
            "components": [
                {"type": "SignalGenerator", "params": {}, "pos": {"x": 0, "y": 0}},
                {"type": "PFBChannelizer", "params": {}, "pos": {"x": 200, "y": 0}}
            ],
            "links": [{"from": 0, "from_port": 0, "to": 1, "to_port": 0}],
            "probe_pins": [],
            "groups": [],
            "network_analyzer": {},
            "window_state": {},
            "graph_state": {}
        })json";
    }

    RfSimulatorApp app;
    app.loadProject(path);
    REQUIRE(app.componentCount() == 2);
    REQUIRE(app.testGraphEngine().links().empty());
    std::remove(path.c_str());
}
