// Regression test for GitHub issue #50:
// "Adding an Equalizer never calls markDirty() - unsaved-changes prompt suppressed"
//
// onAddEqualizer was the only onAdd* callback missing markDirty(), so a user who
// added an Equalizer node and then hit Ctrl+N / Ctrl+O / Exit never saw the
// unsaved-changes modal — the node (and any other unsaved edits) was silently
// discarded. Fix: all component-add callbacks route through
// RfSimulatorApp::addComponent<T>(), which always marks the project dirty.
// This test pins that contract for every current and future onAdd* callback.
#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include <catch2/catch_test_macros.hpp>
#include <functional>

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

TEST_CASE_METHOD(ImGuiFixture, "Every onAdd* callback marks the project dirty (issue #50)",
                 "[app][regression][issue50]") {
    RfSimulatorApp app;
    app.newProject(); // start from an empty, clean graph
    REQUIRE_FALSE(app.isDirty());
    REQUIRE(app.componentCount() == 0);

    NodeGraphWidget &widget = app.testGraphWidget();
    const ImVec2 pos(10.0f, 10.0f);

    struct AddCase {
        const char *name;
        std::function<void(ImVec2)> cb;
    };
    const AddCase cases[] = {
        {"Generator", widget.onAddGenerator},
        {"Amplifier", widget.onAddAmplifier},
        {"Splitter", widget.onAddSplitter},
        {"Mixer", widget.onAddMixer},
        {"ADC", widget.onAddAdc},
        {"PFB", widget.onAddPFB},
        {"Coax Cable", widget.onAddCoaxCable},
        {"Equalizer", widget.onAddEqualizer},
        {"Ideal Filter", widget.onAddIdealFilter},
        {"Attenuator", widget.onAddAttenuator},
        {"Combiner", widget.onAddCombiner},
    };

    for (const auto &c : cases) {
        INFO("onAdd" << c.name);
        REQUIRE_FALSE(app.isDirty());
        const size_t before = app.componentCount();
        c.cb(pos);
        // Every add must dirty the project — otherwise New/Open/Exit silently
        // discards the new node because the unsaved-changes modal never appears.
        REQUIRE(app.isDirty());
        REQUIRE(app.componentCount() == before + 1);
        app.newProject(); // reset to a clean slate for the next callback
        REQUIRE_FALSE(app.isDirty());
    }
}
