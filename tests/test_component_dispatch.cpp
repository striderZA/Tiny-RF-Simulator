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
#include "inspector_panel.h"
#include "pfb_channelizer_engine.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <fstream>
#include <map>
#include <string_view>

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

TEST_CASE_METHOD(ImGuiFixture,
                 "Every registry descriptor has create and draw_inspector (issue #51)",
                 "[dispatch]") {
    // Constructing the app runs InspectorPanel::registerDrawers(), which is
    // the only place draw_inspector is populated; a forgotten branch would
    // otherwise pass CI silently.
    RfSimulatorApp app;
    int next_id = 1;
    for (const auto *d : ComponentTypeRegistry::instance().all()) {
        CAPTURE(d->type);
        REQUIRE(bool(d->create));
        REQUIRE(bool(d->draw_inspector));
        IComponentEngine *engine =
            d->create(app.testComponents(), app.testGraphEngine(), next_id++);
        REQUIRE(engine != nullptr);
        REQUIRE(engine->type_name() == d->type);
    }
}

// Data-driven consistency across the three places a component type is
// declared: ComponentTypeRegistry rows, InspectorPanel drawers, and the
// NodeKind enum. A new type that forgets any one of them fails here loudly
// instead of silently rendering an empty properties panel or an unknown node
// kind. The expected tables are compiled against the enum's current values,
// so dropping a NodeKind or forgetting to register a drawer breaks the build
// or the test.
TEST_CASE("Registry rows, inspector drawers, and NodeKinds stay consistent", "[dispatch]") {
    // Canonical type key -> NodeKind, mirroring ComponentTypeRegistry rows.
    const std::map<std::string_view, NodeKind> expected_kind = {
        {"generator", NodeKind::Generator},
        {"amplifier", NodeKind::Amplifier},
        {"splitter", NodeKind::Splitter},
        {"mixer", NodeKind::Mixer},
        {"adc", NodeKind::Adc},
        {"pfb", NodeKind::PFB},
        {"filter", NodeKind::IdealFilter},
        {"coax", NodeKind::CoaxCable},
        {"equalizer", NodeKind::Equalizer},
        {"attenuator", NodeKind::Attenuator},
        {"combiner", NodeKind::Combiner},
    };
    // supports_sparam_file must be true exactly for the engines that
    // implement the Touchstone S-param API (verified against
    // AmplifierEngine, IdealFilterEngine, EqualizerEngine, AttenuatorEngine,
    // CombinerEngine).
    const std::map<std::string_view, bool> expected_sparam = {
        {"amplifier", true}, {"attenuator", true}, {"combiner", true},  {"equalizer", true},
        {"filter", true},    {"adc", false},       {"coax", false},     {"generator", false},
        {"mixer", false},    {"pfb", false},       {"splitter", false},
    };

    for (const auto *d : ComponentTypeRegistry::instance().all()) {
        CAPTURE(d->type);
        // (a) Every registered type must have an inspector drawer.
        CHECK(InspectorPanel::hasDrawer(d->type));

        // (b) Every type must map to a real NodeKind matching the canonical
        // table; a table miss means the enum doesn't cover the type yet.
        auto kind_it = expected_kind.find(d->type);
        REQUIRE(kind_it != expected_kind.end());
        CHECK(kind_it->second != NodeKind::Unknown);
        CHECK(d->kind == kind_it->second);

        // (c) The S-param capability flag must match engine reality.
        auto sparam_it = expected_sparam.find(d->type);
        REQUIRE(sparam_it != expected_sparam.end());
        CHECK(d->supports_sparam_file == sparam_it->second);
    }
}
