#include "app.h"
#include "circuit_runtime.h"
#include "component_type_registry.h"
#include "editor_commands.h"
#include "graph_editor_actions.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "pfb_view_manager.h"
#include "session_state.h"
#include "test_temp_paths.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

const ComponentTypeDescriptor &descriptor(std::string_view type) {
    const auto *desc = ComponentTypeRegistry::instance().find(type);
    REQUIRE(desc != nullptr);
    return *desc;
}

struct CommandFixture {
    CircuitRuntime runtime;
    GraphEditorActions actions{runtime};
    EditorCommands commands{runtime, actions};
    int components_changed = 0;

    CommandFixture() {
        commands.onComponentsChanged = [this]() { ++components_changed; };
    }
};

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

std::string tempProjectPath(const std::string &name) {
    return (std::filesystem::temp_directory_path() /
            ("editor_commands_" + name + "_" + test_temp_paths::processTag() + ".rfsim"))
        .string();
}

} // namespace

TEST_CASE("Editor commands start clean and an accepted edit makes the project dirty",
          "[editor_commands]") {
    CommandFixture f;
    REQUIRE_FALSE(f.commands.isDirty());

    auto *generator = f.commands.createComponent(descriptor("generator").create);
    REQUIRE(generator != nullptr);
    REQUIRE(f.commands.isDirty());
    REQUIRE(f.components_changed == 1);
}

TEST_CASE("markClean records the saved revision and later edits dirty it again",
          "[editor_commands]") {
    CommandFixture f;
    f.commands.createComponent(descriptor("generator").create);
    const std::uint64_t revision = f.commands.revision();

    f.commands.markClean();
    REQUIRE_FALSE(f.commands.isDirty());
    REQUIRE(f.commands.revision() == revision);

    f.commands.markModified();
    REQUIRE(f.commands.isDirty());
    REQUIRE(f.commands.revision() > revision);
}

TEST_CASE("Rejected topology commands leave the revision untouched", "[editor_commands]") {
    CommandFixture f;
    auto *generator = f.commands.createComponent(descriptor("generator").create);
    auto *amplifier = f.commands.createComponent(descriptor("amplifier").create);
    f.commands.markClean();
    const std::uint64_t clean = f.commands.revision();

    // Input-to-output is not a valid link direction.
    REQUIRE_FALSE(f.commands.connect(amplifier->inputPinId(), generator->outputPinId()));
    REQUIRE_FALSE(f.commands.disconnect(987654));
    REQUIRE_FALSE(f.commands.removeComponent(987654));
    REQUIRE_FALSE(f.commands.removeProbePin(generator->outputPinId()));
    REQUIRE_FALSE(f.commands.removeGroup(987654));
    REQUIRE_FALSE(f.commands.renameGroup(987654, "missing"));
    REQUIRE_FALSE(f.commands.setGroupCollapsed(987654, true));
    REQUIRE(f.commands.revision() == clean);
    REQUIRE_FALSE(f.commands.isDirty());
    REQUIRE(f.components_changed == 2);
}

TEST_CASE("Accepted topology commands each advance the revision", "[editor_commands]") {
    CommandFixture f;
    auto *generator = f.commands.createComponent(descriptor("generator").create);
    auto *amplifier = f.commands.createComponent(descriptor("amplifier").create);
    std::uint64_t revision = f.commands.revision();
    const auto advanced = [&]() {
        const bool moved = f.commands.revision() > revision;
        revision = f.commands.revision();
        return moved;
    };

    const auto link = f.commands.connect(generator->outputPinId(), amplifier->inputPinId());
    REQUIRE(link.has_value());
    REQUIRE(advanced());
    REQUIRE(amplifier->node().inputs[0] == &generator->node().outputs[0]);

    // Probe edits keep their pre-existing semantics: no revision advance.
    REQUIRE(f.commands.addProbePin(generator->outputPinId()));
    REQUIRE_FALSE(advanced());
    REQUIRE(f.commands.removeProbePin(generator->outputPinId()));
    REQUIRE_FALSE(advanced());

    const int group =
        f.commands.createGroup("Chain", {generator->graphNodeId(), amplifier->graphNodeId()});
    REQUIRE(group >= 0);
    REQUIRE(advanced());
    REQUIRE(f.commands.renameGroup(group, "Renamed"));
    REQUIRE(advanced());
    REQUIRE(f.commands.setGroupCollapsed(group, true));
    REQUIRE(advanced());

    // Selection is not project state.
    f.commands.selectGroup(group);
    REQUIRE_FALSE(advanced());

    REQUIRE(f.commands.removeGroup(group));
    REQUIRE(advanced());
    REQUIRE(f.commands.disconnect(*link));
    REQUIRE(advanced());
    REQUIRE(amplifier->node().inputs[0] == nullptr);
}

TEST_CASE("Removing a component notifies component-bound views before returning",
          "[editor_commands]") {
    CommandFixture f;
    auto *generator = f.commands.createComponent(descriptor("generator").create);
    auto *amplifier = f.commands.createComponent(descriptor("amplifier").create);
    REQUIRE(f.commands.connect(generator->outputPinId(), amplifier->inputPinId()));
    const int seen_before = f.components_changed;

    // The hook must observe the post-removal registry, with surviving inputs
    // already rewired, so a view rebuilt from it can never bind a dead engine.
    std::size_t components_at_hook = 0;
    const Spectrum *amp_input_at_hook = &generator->node().outputs[0];
    f.commands.onComponentsChanged = [&]() {
        ++f.components_changed;
        components_at_hook = f.runtime.components().size();
        amp_input_at_hook = amplifier->node().inputs[0];
    };

    REQUIRE(f.commands.removeComponent(generator->graphNodeId()));
    REQUIRE(f.components_changed == seen_before + 1);
    REQUIRE(components_at_hook == 1);
    REQUIRE(amp_input_at_hook == nullptr);
}

TEST_CASE("componentsAdded adopts components created outside the command service",
          "[editor_commands]") {
    CommandFixture f;
    // e.g. ComponentLibrary::instantiate() creates through the runtime directly.
    f.runtime.createComponent(descriptor("amplifier").create);
    REQUIRE_FALSE(f.commands.isDirty());

    f.commands.componentsAdded();
    REQUIRE(f.commands.isDirty());
    REQUIRE(f.components_changed == 1);
}

TEST_CASE("PFB view sync follows the registry and keeps surviving views' visibility",
          "[editor_commands][pfb_views]") {
    CircuitRuntime runtime;
    SessionState state;
    PFBViewManager views;
    const auto &pfb = descriptor("pfb");

    auto *first = runtime.createComponent(pfb.create);
    auto *second = runtime.createComponent(pfb.create);
    views.sync(runtime.components(), state);
    REQUIRE(views.size() == 2);

    // Toggle away from whatever the session file holds, so preservation is
    // distinguishable from a reload of the persisted value.
    const bool first_iq = !views.iqVisibility()[0];
    const bool second_grid = !views.gridVisibility()[1];
    views.iqVisibility()[0] = first_iq;
    views.gridVisibility()[1] = second_grid;

    runtime.createComponent(pfb.create);
    views.sync(runtime.components(), state);
    REQUIRE(views.size() == 3);
    REQUIRE(views.iqVisibility()[0] == first_iq);
    REQUIRE(views.gridVisibility()[1] == second_grid);

    REQUIRE(runtime.removeComponent(first->graphNodeId()));
    views.sync(runtime.components(), state);
    REQUIRE(views.size() == 2);
    // The second PFB is now first in registry order and kept its own flag.
    REQUIRE(views.gridVisibility()[0] == second_grid);
    (void)second;

    views.clear();
    REQUIRE(views.size() == 0);
}

TEST_CASE_METHOD(ImGuiFixture, "App test commands share the editor's side effects",
                 "[editor_commands][app]") {
    RfSimulatorApp app;
    REQUIRE_FALSE(app.isDirty());

    auto *pfb = app.testCreateComponent("pfb", 9001);
    REQUIRE(pfb != nullptr);
    REQUIRE(app.testPfbViewCount() == 1);
    REQUIRE(app.isDirty());

    // A component removal through the hook is a real edit after a save.
    const std::string path = tempProjectPath("remove");
    app.saveProject(path);
    REQUIRE_FALSE(app.isDirty());
    const std::uint64_t saved = app.projectRevision();

    REQUIRE(app.testRemoveComponent(pfb->graphNodeId()));
    REQUIRE(app.testPfbViewCount() == 0);
    REQUIRE(app.isDirty());
    REQUIRE(app.projectRevision() > saved);
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "Project load and New rebuild PFB views from the circuit",
                 "[editor_commands][app]") {
    const std::string path = tempProjectPath("pfb_views");
    {
        RfSimulatorApp app;
        REQUIRE(app.testCreateComponent("pfb", 9101) != nullptr);
        REQUIRE(app.testCreateComponent("pfb", 9102) != nullptr);
        app.saveProject(path);
        REQUIRE_FALSE(app.isDirty());
    }
    {
        RfSimulatorApp app;
        REQUIRE(app.testPfbViewCount() == 0);
        app.loadProject(path);
        REQUIRE_FALSE(app.isDirty());
        REQUIRE(app.testPfbViewCount() == 2);

        app.newProject();
        REQUIRE(app.testPfbViewCount() == 0);
        REQUIRE_FALSE(app.isDirty());
    }
    std::filesystem::remove(path);
}
