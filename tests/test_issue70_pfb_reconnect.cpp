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
#include "test_temp_paths.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
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

    auto &gen = static_cast<SignalGeneratorEngine &>(*app.testCreateComponent("generator", 10001));
    gen.addTone(100e6, -20.0);
    auto &adc = static_cast<AdcEngine &>(*app.testCreateComponent("adc", 10002));
    auto &pfb = static_cast<PFBChannelizerEngine &>(*app.testCreateComponent("pfb", 10003));

    REQUIRE_FALSE(app.testConnectLink(gen.outputPinId(), pfb.inputPinId()).has_value());
    app.update_dsp();
    REQUIRE(pfb.node().inputs[0] == nullptr);
    REQUIRE(pfb.fs_Hz() == 0.0);
    REQUIRE(pfb.node().outputs[0].frequencies.empty());

    REQUIRE(app.testConnectLink(gen.outputPinId(), adc.inputPinId()).has_value());
    REQUIRE(app.testConnectLink(adc.outputPinId(), pfb.inputPinId()).has_value());
    app.update_dsp();
    REQUIRE(pfb.fs_Hz() == Catch::Approx(500e6));
    REQUIRE_FALSE(pfb.node().outputs[0].frequencies.empty());

    REQUIRE(app.testRemoveComponent(adc.graphNodeId()));
    app.update_dsp();
    REQUIRE(pfb.node().inputs[0] == nullptr);

    REQUIRE_FALSE(app.testConnectLink(gen.outputPinId(), pfb.inputPinId()).has_value());
    app.update_dsp();
    REQUIRE(pfb.node().inputs[0] == nullptr);
    REQUIRE(pfb.fs_Hz() == 0.0);
    REQUIRE(pfb.node().outputs[0].frequencies.empty());
    REQUIRE(pfb.node().outputs[1].frequencies.empty());
}

TEST_CASE_METHOD(Issue70ImGuiFixture, "Project load rejects direct PFB input links (issue #70)",
                 "[app][pfb][regression][issue70][project]") {
    // Absolute temp path: this standalone executable runs with the source tree
    // as its CTest working directory, so a relative name would write into it.
    const std::string path =
        (std::filesystem::temp_directory_path() /
         ("test_issue70_invalid_link_" + test_temp_paths::processTag() + ".rfsim"))
            .string();
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
