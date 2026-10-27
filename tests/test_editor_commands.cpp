#include "app.h"
#include "circuit_runtime.h"
#include "component_library.h"
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
#include <map>
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
        // Headless frames need a display size and a built font atlas (no renderer backend here).
        ImGui::GetIO().DisplaySize = ImVec2(1920, 1080);
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char *atlas_pixels = nullptr;
        int atlas_w = 0, atlas_h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&atlas_pixels, &atlas_w, &atlas_h);
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

std::string zx60DefinitionPath() {
    return std::string(PROJECT_SOURCE_DIR) +
           "/component_data/library/amplifiers/mini-circuits/zx60-33ln.json";
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

    // Probe add and remove are project edits (issue #204): they advance the revision.
    REQUIRE(f.commands.addProbePin(generator->outputPinId()));
    REQUIRE(advanced());
    REQUIRE(f.commands.removeProbePin(generator->outputPinId()));
    REQUIRE(advanced());

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

TEST_CASE("addLibraryPart adds the part in one revision", "[editor_commands][agent]") {
    CommandFixture f;
    ComponentLibrary library;
    library.loadFile(zx60DefinitionPath());
    const auto definitions = library.all();
    REQUIRE(definitions.size() == 1);

    const std::uint64_t revision_before = f.commands.revision();
    const ComponentAddResult result = f.commands.addLibraryPart(library, *definitions.front());

    REQUIRE(result.component != nullptr);
    CHECK(result.params.status == ParamWriteStatus::Unchanged);
    CHECK(result.params.ok());
    CHECK(f.runtime.components().size() == 1);
    CHECK(f.commands.revision() == revision_before + 1);
    CHECK(f.components_changed == 1);

    bool found_part_number = false;
    for (const auto &node : f.runtime.graph().nodes()) {
        if (node.node_id == result.component->graphNodeId()) {
            CHECK(node.part_number == "ZX60-33LN+");
            found_part_number = true;
        }
    }
    CHECK(found_part_number);
}

TEST_CASE("Parameters given to addLibraryPart override the part's values",
          "[editor_commands][agent]") {
    CommandFixture f;
    ComponentLibrary library;
    library.loadFile(zx60DefinitionPath());
    const auto definitions = library.all();
    REQUIRE(definitions.size() == 1);

    const std::uint64_t revision_before = f.commands.revision();
    const nlohmann::ordered_json params = {{"gain_dB", 20}};
    const ComponentAddResult result =
        f.commands.addLibraryPart(library, *definitions.front(), params);

    REQUIRE(result.component != nullptr);
    CHECK(result.params.status == ParamWriteStatus::Applied);
    const nlohmann::json state = result.component->serialize();
    CHECK(state["gain_dB"].get<double>() == 20.0);
    CHECK(state["nf_dB"].get<double>() == 1.1);
    CHECK(f.commands.revision() == revision_before + 1);
    CHECK(f.components_changed == 1);
}

TEST_CASE("An add whose parameters are rejected leaves nothing behind",
          "[editor_commands][agent]") {
    CommandFixture f;
    const std::size_t component_count_before = f.runtime.components().size();
    const std::uint64_t revision_before = f.commands.revision();
    const int callbacks_before = f.components_changed;
    const nlohmann::ordered_json params = {{"nope", 1}};

    const ComponentAddResult result =
        f.commands.createComponentWithParams(descriptor("amplifier").create, params);

    CHECK(result.component == nullptr);
    CHECK(result.params.status == ParamWriteStatus::UnknownKey);
    CHECK(f.runtime.components().size() == component_count_before);
    CHECK(f.commands.revision() == revision_before);
    CHECK(f.components_changed == callbacks_before);
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
    // A probe add or remove is a project edit too (issue #204), so it dirties the project.
    auto *generator = app.testCreateComponent("generator", 9002);
    REQUIRE(generator != nullptr);
    app.saveProject(path);
    REQUIRE_FALSE(app.isDirty());
    const int pin = generator->outputPinId();
    REQUIRE(app.testAddProbePin(pin));
    REQUIRE(app.testGraphEngine().probePins() == std::vector<int>{pin});
    REQUIRE(app.isDirty());
    app.saveProject(path);
    REQUIRE_FALSE(app.isDirty());
    // A refused duplicate add changes no project state.
    REQUIRE_FALSE(app.testAddProbePin(pin));
    REQUIRE_FALSE(app.isDirty());
    REQUIRE(app.testRemoveProbePin(pin));
    REQUIRE(app.testGraphEngine().probePins().empty());
    REQUIRE(app.isDirty());
    app.saveProject(path);
    REQUIRE_FALSE(app.isDirty());
    // A refused remove of an absent probe changes no project state.
    REQUIRE_FALSE(app.testRemoveProbePin(pin));
    REQUIRE_FALSE(app.isDirty());
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

namespace {

// Stands in for the widget's imnodes positions: the command reads and writes through the hooks.
struct PositionStore {
    std::map<int, LayoutPoint> positions;

    void attach(EditorCommands &commands) {
        commands.readNodePosition = [this](int node_id) { return positions[node_id]; };
        commands.writeNodePosition = [this](int node_id, LayoutPoint position) {
            positions[node_id] = position;
        };
    }

    bool at(int node_id, float x, float y) const {
        const auto it = positions.find(node_id);
        return it != positions.end() && it->second.x == x && it->second.y == y;
    }
};

} // namespace

TEST_CASE("moveNodes places every listed node as one revision and one-step undo restores them",
          "[editor_commands][layout]") {
    CommandFixture f;
    PositionStore store;
    store.attach(f.commands);
    const int a = f.commands.createComponent(descriptor("generator").create)->graphNodeId();
    const int b = f.commands.createComponent(descriptor("amplifier").create)->graphNodeId();
    store.positions[a] = LayoutPoint{10.0f, 20.0f};
    store.positions[b] = LayoutPoint{300.0f, 40.0f};

    const std::uint64_t before = f.commands.revision();
    REQUIRE(f.commands.moveNodes({{a, {50.0f, 20.0f}}, {b, {50.0f, 40.0f}}}).empty());
    REQUIRE(store.at(a, 50.0f, 20.0f));
    REQUIRE(store.at(b, 50.0f, 40.0f));
    REQUIRE(f.commands.revision() == before + 1);
    REQUIRE(f.commands.canUndoLayout());

    REQUIRE(f.commands.undoLayout());
    REQUIRE(store.at(a, 10.0f, 20.0f));
    REQUIRE(store.at(b, 300.0f, 40.0f));
    REQUIRE(f.commands.revision() == before + 2);

    SECTION("undo is one step: a second undo has nothing to restore") {
        REQUIRE_FALSE(f.commands.canUndoLayout());
        REQUIRE_FALSE(f.commands.undoLayout());
        REQUIRE(store.at(a, 10.0f, 20.0f));
    }
}

TEST_CASE("moveNodes refuses a node inside a collapsed subcircuit and changes nothing",
          "[editor_commands][layout]") {
    CommandFixture f;
    PositionStore store;
    store.attach(f.commands);
    const int a = f.commands.createComponent(descriptor("generator").create)->graphNodeId();
    const int b = f.commands.createComponent(descriptor("amplifier").create)->graphNodeId();
    store.positions[a] = LayoutPoint{10.0f, 20.0f};
    store.positions[b] = LayoutPoint{300.0f, 40.0f};
    const int group = f.commands.createGroup("Front end", {a, b});
    REQUIRE(group != -1);
    REQUIRE(f.commands.setGroupCollapsed(group, true));
    const std::uint64_t revision = f.commands.revision();

    const std::string error = f.commands.moveNodes({{a, {99.0f, 99.0f}}, {b, {99.0f, 40.0f}}});
    REQUIRE_FALSE(error.empty());
    REQUIRE(error.find("Front end") != std::string::npos);
    REQUIRE(store.at(a, 10.0f, 20.0f));
    REQUIRE(store.at(b, 300.0f, 40.0f));
    REQUIRE(f.commands.revision() == revision);
    REQUIRE_FALSE(f.commands.canUndoLayout());
}

TEST_CASE("a layout that moves nothing leaves the revision and the undo record alone",
          "[editor_commands][layout]") {
    CommandFixture f;
    PositionStore store;
    store.attach(f.commands);
    const int a = f.commands.createComponent(descriptor("generator").create)->graphNodeId();
    const int b = f.commands.createComponent(descriptor("amplifier").create)->graphNodeId();
    store.positions[a] = LayoutPoint{10.0f, 20.0f};
    store.positions[b] = LayoutPoint{300.0f, 40.0f};
    REQUIRE(f.commands.moveNodes({{a, {50.0f, 20.0f}}, {b, {50.0f, 40.0f}}}).empty());
    const std::uint64_t revision = f.commands.revision();

    REQUIRE(f.commands.moveNodes({{a, {50.0f, 20.0f}}, {b, {50.0f, 40.0f}}}).empty());
    REQUIRE(f.commands.revision() == revision);
    REQUIRE(f.commands.canUndoLayout());
    REQUIRE(f.commands.undoLayout());
    REQUIRE(store.at(a, 10.0f, 20.0f));
}

TEST_CASE("a later project edit makes the layout undo unavailable", "[editor_commands][layout]") {
    CommandFixture f;
    PositionStore store;
    store.attach(f.commands);
    const int a = f.commands.createComponent(descriptor("generator").create)->graphNodeId();
    const int b = f.commands.createComponent(descriptor("amplifier").create)->graphNodeId();
    store.positions[a] = LayoutPoint{10.0f, 20.0f};
    store.positions[b] = LayoutPoint{300.0f, 40.0f};
    REQUIRE(f.commands.moveNodes({{a, {50.0f, 20.0f}}, {b, {50.0f, 40.0f}}}).empty());

    f.commands.createComponent(descriptor("generator").create);
    REQUIRE_FALSE(f.commands.canUndoLayout());
    REQUIRE_FALSE(f.commands.undoLayout());
    REQUIRE(store.at(a, 50.0f, 20.0f));
}

TEST_CASE("moveNodes refuses a list that names the same node twice", "[editor_commands][layout]") {
    CommandFixture f;
    PositionStore store;
    store.attach(f.commands);
    const int a = f.commands.createComponent(descriptor("generator").create)->graphNodeId();
    store.positions[a] = LayoutPoint{10.0f, 20.0f};
    const std::uint64_t revision = f.commands.revision();

    REQUIRE_FALSE(f.commands.moveNodes({{a, {50.0f, 20.0f}}, {a, {60.0f, 20.0f}}}).empty());
    REQUIRE(store.at(a, 10.0f, 20.0f));
    REQUIRE(f.commands.revision() == revision);
}

namespace {

// Selects exactly `ids` in the widget's ImNodes context, as a box or Ctrl-click selection does.
void selectNodes(NodeGraphWidget &widget, const std::vector<int> &ids) {
    ImNodes::EditorContextSet(widget.context());
    ImNodes::ClearNodeSelection();
    for (int id : ids)
        ImNodes::SelectNode(id);
}

// Draws one headless frame of the node editor, as the app does on each update.
void drawFrame(NodeGraphWidget &widget) {
    ImGui::NewFrame();
    bool open = true;
    widget.draw("Node Editor", &open);
    ImGui::EndFrame();
}

} // namespace

TEST_CASE_METHOD(ImGuiFixture,
                 "Node editor align moves the selected nodes and one-step undo restores them",
                 "[editor_commands][app][layout]") {
    RfSimulatorApp app;
    NodeGraphWidget &widget = app.testGraphWidget();
    const int a = app.testCreateComponent("generator", 9201)->graphNodeId();
    const int b = app.testCreateComponent("amplifier", 9202)->graphNodeId();
    widget.setNodeEditorSpacePosition(a, ImVec2(100.0f, 40.0f));
    widget.setNodeEditorSpacePosition(b, ImVec2(300.0f, 60.0f));
    selectNodes(widget, {a, b});

    widget.alignSelection(AlignMode::Top);
    REQUIRE(widget.layoutMessage().empty());
    REQUIRE(widget.editorPositionForSave(a).y == 40.0f);
    REQUIRE(widget.editorPositionForSave(b).y == 40.0f);
    REQUIRE(widget.editorPositionForSave(b).x == 300.0f);

    REQUIRE(app.testEditorCommands().undoLayout());
    REQUIRE(widget.editorPositionForSave(a).y == 40.0f);
    REQUIRE(widget.editorPositionForSave(b).y == 60.0f);
}

TEST_CASE_METHOD(ImGuiFixture,
                 "Node editor refuses align when a collapsed member is still selected after frames",
                 "[editor_commands][app][layout]") {
    RfSimulatorApp app;
    NodeGraphWidget &widget = app.testGraphWidget();
    const int a = app.testCreateComponent("generator", 9301)->graphNodeId();
    const int b = app.testCreateComponent("amplifier", 9302)->graphNodeId();
    const int c = app.testCreateComponent("generator", 9303)->graphNodeId();
    widget.setNodeEditorSpacePosition(a, ImVec2(100.0f, 40.0f));
    widget.setNodeEditorSpacePosition(b, ImVec2(300.0f, 60.0f));
    widget.setNodeEditorSpacePosition(c, ImVec2(500.0f, 80.0f));
    drawFrame(widget);
    selectNodes(widget, {a, b, c});
    const int group = app.testCreateGroup("Front end", {a, b});
    REQUIRE(group != -1);
    REQUIRE(app.testSetGroupCollapsed(group, true));
    // The first frame after the collapse drops the hidden members from ImNodes; later frames
    // must still name the subcircuit.
    drawFrame(widget);
    drawFrame(widget);

    // Only C is a live entry, so the Align entries must not count the two stale ones.
    REQUIRE(widget.layoutSelectionCount() == 1);
    widget.alignSelection(AlignMode::Top);
    REQUIRE(widget.layoutMessage().find("\"Front end\"") != std::string::npos);
    REQUIRE(widget.layoutMessage().find("no longer") == std::string::npos);
    REQUIRE(widget.editorPositionForSave(c).y == 80.0f);
    REQUIRE_FALSE(app.testEditorCommands().canUndoLayout());
}

TEST_CASE_METHOD(ImGuiFixture,
                 "Node editor refuses align for a collapsed member before any frame is drawn",
                 "[editor_commands][app][layout]") {
    RfSimulatorApp app;
    NodeGraphWidget &widget = app.testGraphWidget();
    const int a = app.testCreateComponent("generator", 9311)->graphNodeId();
    const int b = app.testCreateComponent("amplifier", 9312)->graphNodeId();
    const int c = app.testCreateComponent("generator", 9313)->graphNodeId();
    widget.setNodeEditorSpacePosition(a, ImVec2(100.0f, 40.0f));
    widget.setNodeEditorSpacePosition(b, ImVec2(300.0f, 60.0f));
    widget.setNodeEditorSpacePosition(c, ImVec2(500.0f, 80.0f));
    selectNodes(widget, {a, b, c});
    const int group = app.testCreateGroup("Front end", {a, b});
    REQUIRE(group != -1);
    REQUIRE(app.testSetGroupCollapsed(group, true));

    widget.alignSelection(AlignMode::Top);
    REQUIRE(widget.layoutMessage().find("\"Front end\"") != std::string::npos);
    REQUIRE(widget.editorPositionForSave(c).y == 80.0f);
    REQUIRE_FALSE(app.testEditorCommands().canUndoLayout());
}

TEST_CASE_METHOD(ImGuiFixture,
                 "Node editor refuses align when a collapsed subcircuit block is selected",
                 "[editor_commands][app][layout]") {
    RfSimulatorApp app;
    NodeGraphWidget &widget = app.testGraphWidget();
    const int a = app.testCreateComponent("generator", 9321)->graphNodeId();
    const int b = app.testCreateComponent("amplifier", 9322)->graphNodeId();
    const int c = app.testCreateComponent("generator", 9323)->graphNodeId();
    widget.setNodeEditorSpacePosition(a, ImVec2(100.0f, 40.0f));
    widget.setNodeEditorSpacePosition(b, ImVec2(300.0f, 60.0f));
    widget.setNodeEditorSpacePosition(c, ImVec2(500.0f, 80.0f));
    const int group = app.testCreateGroup("Front end", {a, b});
    REQUIRE(group != -1);
    REQUIRE(app.testSetGroupCollapsed(group, true));
    drawFrame(widget); // draws the collapsed block
    selectNodes(widget, {group, c});

    widget.alignSelection(AlignMode::Top);
    REQUIRE(widget.layoutMessage().find("collapsed subcircuit \"Front end\"") != std::string::npos);
    REQUIRE(widget.editorPositionForSave(c).y == 80.0f);
}

TEST_CASE_METHOD(ImGuiFixture,
                 "Expanding a selected subcircuit leaves stale entries that do not refuse layout",
                 "[editor_commands][app][layout]") {
    RfSimulatorApp app;
    NodeGraphWidget &widget = app.testGraphWidget();
    const int a = app.testCreateComponent("generator", 9331)->graphNodeId();
    const int b = app.testCreateComponent("amplifier", 9332)->graphNodeId();
    const int c = app.testCreateComponent("generator", 9333)->graphNodeId();
    widget.setNodeEditorSpacePosition(a, ImVec2(100.0f, 40.0f));
    widget.setNodeEditorSpacePosition(b, ImVec2(300.0f, 60.0f));
    widget.setNodeEditorSpacePosition(c, ImVec2(500.0f, 80.0f));
    drawFrame(widget);
    const int group = app.testCreateGroup("Front end", {a, b});
    REQUIRE(group != -1);
    REQUIRE(app.testSetGroupCollapsed(group, true));
    drawFrame(widget);
    selectNodes(widget, {group});
    drawFrame(widget);
    // Expanding drops the block from ImNodes, leaving a stale entry in the selection.
    REQUIRE(app.testSetGroupCollapsed(group, false));
    drawFrame(widget);

    // Select the expanded members without clearing, so the stale block entry stays in ImNodes.
    ImNodes::EditorContextSet(widget.context());
    ImNodes::SelectNode(a);
    ImNodes::SelectNode(b);
    REQUIRE(widget.layoutSelectionCount() == 2);
    widget.alignSelection(AlignMode::Top);
    REQUIRE(widget.layoutMessage().empty());
    REQUIRE(widget.editorPositionForSave(a).y == 40.0f);
    REQUIRE(widget.editorPositionForSave(b).y == 40.0f);
    REQUIRE(widget.editorPositionForSave(c).y == 80.0f);
}

TEST_CASE_METHOD(ImGuiFixture, "New project drops the layout undo record",
                 "[editor_commands][app][layout]") {
    RfSimulatorApp app;
    NodeGraphWidget &widget = app.testGraphWidget();
    const int a = app.testCreateComponent("generator", 9401)->graphNodeId();
    const int b = app.testCreateComponent("amplifier", 9402)->graphNodeId();
    widget.setNodeEditorSpacePosition(a, ImVec2(100.0f, 40.0f));
    widget.setNodeEditorSpacePosition(b, ImVec2(300.0f, 60.0f));
    selectNodes(widget, {a, b});
    widget.alignSelection(AlignMode::Top);
    REQUIRE(app.testEditorCommands().canUndoLayout());

    app.newProject();
    REQUIRE_FALSE(app.testEditorCommands().canUndoLayout());
}

TEST_CASE_METHOD(ImGuiFixture, "Grid snapping is off by default and the widget toggle sets it",
                 "[editor_commands][app][grid]") {
    RfSimulatorApp app;
    NodeGraphWidget &widget = app.testGraphWidget();
    const int a = app.testCreateComponent("generator", 9501)->graphNodeId();
    widget.setNodeEditorSpacePosition(a, ImVec2(101.0f, 43.0f));
    const auto snapFlagSet = []() {
        return (ImNodes::GetStyle().Flags & ImNodesStyleFlags_GridSnapping) != 0;
    };

    drawFrame(widget);
    REQUIRE_FALSE(widget.gridSnapping());
    REQUIRE_FALSE(snapFlagSet());

    widget.setGridSnapping(true);
    drawFrame(widget);
    REQUIRE(snapFlagSet());

    widget.setGridSnapping(false);
    drawFrame(widget);
    REQUIRE_FALSE(snapFlagSet());
}

TEST_CASE_METHOD(ImGuiFixture, "Snap selection to grid rounds each selected node onto the grid",
                 "[editor_commands][app][grid]") {
    RfSimulatorApp app;
    NodeGraphWidget &widget = app.testGraphWidget();
    const int a = app.testCreateComponent("generator", 9511)->graphNodeId();
    const int b = app.testCreateComponent("amplifier", 9512)->graphNodeId();
    const int c = app.testCreateComponent("generator", 9513)->graphNodeId();
    widget.setNodeEditorSpacePosition(a, ImVec2(101.0f, 43.0f));
    widget.setNodeEditorSpacePosition(b, ImVec2(305.0f, 61.0f));
    widget.setNodeEditorSpacePosition(c, ImVec2(101.0f, 43.0f));
    ImNodes::EditorContextSet(widget.context());
    ImNodes::EditorContextResetPanning(ImVec2(0.0f, 0.0f));
    selectNodes(widget, {a, b});
    const std::uint64_t revision = app.projectRevision();

    widget.snapSelectionToGrid();

    REQUIRE(widget.layoutMessage().empty());
    REQUIRE(widget.editorPositionForSave(a).x == 96.0f);
    REQUIRE(widget.editorPositionForSave(a).y == 48.0f);
    REQUIRE(widget.editorPositionForSave(b).x == 312.0f);
    REQUIRE(widget.editorPositionForSave(b).y == 72.0f);
    REQUIRE(widget.editorPositionForSave(c).x == 101.0f);
    REQUIRE(widget.editorPositionForSave(c).y == 43.0f);
    REQUIRE(app.projectRevision() != revision);
    // The command restores the drag-snap bit it borrowed, so the toggle stays off.
    REQUIRE_FALSE(widget.gridSnapping());
    REQUIRE((ImNodes::GetStyle().Flags & ImNodesStyleFlags_GridSnapping) == 0);
}

TEST_CASE_METHOD(ImGuiFixture,
                 "Snap selection to grid on an already snapped selection changes nothing",
                 "[editor_commands][app][grid]") {
    RfSimulatorApp app;
    NodeGraphWidget &widget = app.testGraphWidget();
    const int a = app.testCreateComponent("generator", 9521)->graphNodeId();
    const int b = app.testCreateComponent("amplifier", 9522)->graphNodeId();
    widget.setNodeEditorSpacePosition(a, ImVec2(96.0f, 48.0f));
    widget.setNodeEditorSpacePosition(b, ImVec2(312.0f, 72.0f));
    widget.setGridSnapping(true);
    drawFrame(widget);
    selectNodes(widget, {a, b});
    const std::uint64_t revision = app.projectRevision();

    widget.snapSelectionToGrid();

    REQUIRE(app.projectRevision() == revision);
    REQUIRE(widget.editorPositionForSave(a).x == 96.0f);
    REQUIRE(widget.editorPositionForSave(b).y == 72.0f);
    REQUIRE(widget.gridSnapping());
    REQUIRE((ImNodes::GetStyle().Flags & ImNodesStyleFlags_GridSnapping) != 0);
}

TEST_CASE_METHOD(ImGuiFixture, "Snap selection to grid refuses a selected collapsed subcircuit",
                 "[editor_commands][app][grid]") {
    RfSimulatorApp app;
    NodeGraphWidget &widget = app.testGraphWidget();
    const int a = app.testCreateComponent("generator", 9531)->graphNodeId();
    const int b = app.testCreateComponent("amplifier", 9532)->graphNodeId();
    const int c = app.testCreateComponent("generator", 9533)->graphNodeId();
    widget.setNodeEditorSpacePosition(a, ImVec2(101.0f, 43.0f));
    widget.setNodeEditorSpacePosition(b, ImVec2(305.0f, 61.0f));
    widget.setNodeEditorSpacePosition(c, ImVec2(101.0f, 43.0f));
    const int group = app.testCreateGroup("Front end", {a, b});
    REQUIRE(group != -1);
    REQUIRE(app.testSetGroupCollapsed(group, true));
    drawFrame(widget); // draws the collapsed block
    selectNodes(widget, {group, c});
    const std::uint64_t revision = app.projectRevision();

    widget.snapSelectionToGrid();

    REQUIRE(widget.layoutMessage().find("collapsed subcircuit \"Front end\"") != std::string::npos);
    REQUIRE(widget.editorPositionForSave(c).x == 101.0f);
    REQUIRE(app.projectRevision() == revision);
}
