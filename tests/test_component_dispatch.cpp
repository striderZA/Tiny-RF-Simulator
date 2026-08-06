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
#include <catch2/catch_test_macros.hpp>

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
