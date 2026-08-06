// Regression test for issue #51: adding an Equalizer from the canvas context
// menu must mark the project dirty.
//
// Root cause: the old onAddEqualizer lambda in RfSimulatorApp's constructor
// (app.cpp) forgot to call markDirty(), unlike every other onAdd* lambda. The
// unified RfSimulatorApp::addComponent() path (data-driven canvas menu built
// from ComponentTypeRegistry::all()) calls markDirty() unconditionally, which
// fixes the bug.
#include "app.h"
#include "component_type_registry.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "pfb_channelizer_engine.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <fstream>

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

TEST_CASE_METHOD(ImGuiFixture, "Adding an Equalizer marks the project dirty (issue #51)",
                 "[dispatch][regression]") {
    RfSimulatorApp app;
    REQUIRE(app.isDirty() == false);
    bool clicked = false;
    for (const auto &addable : app.testGraphWidget().addableComponents()) {
        if (addable.menu_label == "Add Equalizer") {
            addable.on_add(ImVec2(0, 0));
            clicked = true;
        }
    }
    REQUIRE(clicked);
    REQUIRE(app.isDirty() == true);
}

TEST_CASE_METHOD(ImGuiFixture, "Every registry label_prefix maps to its kind", "[dispatch]") {
    RfSimulatorApp app;
    for (const auto *d : ComponentTypeRegistry::instance().all()) {
        REQUIRE(app.testGraphWidget().kindForLabel(d->label_prefix + " 1") == d->kind);
    }
    REQUIRE(app.testGraphWidget().kindForLabel("UnknownThing 1") == NodeKind::Unknown);
}

TEST_CASE_METHOD(ImGuiFixture, "All 11 registry types round-trip through project save/load",
                 "[dispatch]") {
    auto path = "test_dispatch_all_types.rfsim";
    std::remove(path);
    {
        RfSimulatorApp app;
        app.newProject();
        for (const auto &addable : app.testGraphWidget().addableComponents())
            addable.on_add(ImVec2(0, 0));
        REQUIRE(app.componentCount() == 11);
        app.saveProject(path);
    }
    {
        RfSimulatorApp app;
        app.loadProject(path);
        REQUIRE(app.componentCount() == 11);
    }
    std::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "Legacy .rfsim type strings still load (backward compat)",
                 "[dispatch]") {
    auto path = "test_dispatch_legacy.rfsim";
    std::ofstream out(path);
    out << R"({
      "version": 1,
      "name": "legacy",
      "components": [
        {"type": "SignalGenerator", "params": {"tones": [{"freq_Hz": 100e6, "power_dBm": -20.0, "phase_deg": 0.0}]}},
        {"type": "Amplifier", "params": {"gain_dB": 10.0, "nf_dB": 2.0}},
        {"type": "ADC", "params": {"sample_rate_Hz": 1e9}},
        {"type": "IdealFilter", "params": {"filter_type": 1}},
        {"type": "PFBChannelizer", "params": {}},
        {"type": "CoaxCable", "params": {}}
      ],
      "links": [],
      "groups": []
    })";
    out.close();
    RfSimulatorApp app;
    app.loadProject(path);
    REQUIRE(app.componentCount() == 6);
    REQUIRE(app.testComponents().byType<PFBChannelizerEngine>().size() == 1);
    std::remove(path);
}
