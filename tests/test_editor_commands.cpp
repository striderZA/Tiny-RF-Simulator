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
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stdexcept>
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
    // Number of writes that throw before they store, so a test can fail one undo/redo step once.
    int fail_next_writes = 0;

    void attach(EditorCommands &commands) {
        commands.readNodePosition = [this](int node_id) { return positions[node_id]; };
        commands.writeNodePosition = [this](int node_id, LayoutPoint position) {
            if (fail_next_writes-- > 0)
                throw std::runtime_error("position write failed");
            positions[node_id] = position;
        };
        // Pan is zero here, so a node's editor position is its grid position.
        commands.readNodeGridPosition = [this](int node_id) { return positions[node_id]; };
        commands.writeNodeGridPosition = [this](int node_id, LayoutPoint grid_position) {
            if (fail_next_writes-- > 0)
                throw std::runtime_error("position write failed");
            positions[node_id] = grid_position;
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
    const std::size_t records_before = f.commands.undoCount();
    REQUIRE(f.commands.moveNodes({{a, {50.0f, 20.0f}}, {b, {50.0f, 40.0f}}}).empty());
    REQUIRE(store.at(a, 50.0f, 20.0f));
    REQUIRE(store.at(b, 50.0f, 40.0f));
    REQUIRE(f.commands.revision() == before + 1);
    REQUIRE(f.commands.undoCount() == records_before + 1);
    REQUIRE(f.commands.canUndo());
    REQUIRE(f.commands.undoLabel() == "Arrange nodes");

    REQUIRE(f.commands.undo());
    REQUIRE(store.at(a, 10.0f, 20.0f));
    REQUIRE(store.at(b, 300.0f, 40.0f));
    REQUIRE(f.commands.revision() == before + 2);

    // Undo is one step: the layout record is consumed, and the earlier component adds are next.
    REQUIRE(f.commands.undoCount() == records_before);
    REQUIRE(f.commands.undoLabel() != "Arrange nodes");
    REQUIRE(f.commands.redoLabel() == "Arrange nodes");

    REQUIRE(f.commands.redo());
    REQUIRE(store.at(a, 50.0f, 20.0f));
    REQUIRE(store.at(b, 50.0f, 40.0f));
    REQUIRE(f.commands.undoLabel() == "Arrange nodes");
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
    const std::size_t records = f.commands.undoCount();

    const std::string error = f.commands.moveNodes({{a, {99.0f, 99.0f}}, {b, {99.0f, 40.0f}}});
    REQUIRE_FALSE(error.empty());
    REQUIRE(error.find("Front end") != std::string::npos);
    REQUIRE(store.at(a, 10.0f, 20.0f));
    REQUIRE(store.at(b, 300.0f, 40.0f));
    REQUIRE(f.commands.revision() == revision);
    REQUIRE(f.commands.undoCount() == records);
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
    const std::size_t records = f.commands.undoCount();

    SECTION("the earlier layout stays undoable and the no-op adds no record") {
        REQUIRE(f.commands.moveNodes({{a, {50.0f, 20.0f}}, {b, {50.0f, 40.0f}}}).empty());
        REQUIRE(f.commands.revision() == revision);
        REQUIRE(f.commands.undoCount() == records);
        REQUIRE(f.commands.undoLabel() == "Arrange nodes");
        REQUIRE(f.commands.undo());
        REQUIRE(store.at(a, 10.0f, 20.0f));
        REQUIRE(store.at(b, 300.0f, 40.0f));
    }

    SECTION("a no-op layout from an empty history leaves nothing to undo") {
        f.commands.clearHistory();
        REQUIRE_FALSE(f.commands.canUndo());

        REQUIRE(f.commands.moveNodes({{a, {50.0f, 20.0f}}, {b, {50.0f, 40.0f}}}).empty());
        REQUIRE(f.commands.revision() == revision);
        REQUIRE_FALSE(f.commands.canUndo());
    }
}

TEST_CASE("a later project edit is undone before the layout", "[editor_commands][layout]") {
    CommandFixture f;
    PositionStore store;
    store.attach(f.commands);
    const int a = f.commands.createComponent(descriptor("generator").create)->graphNodeId();
    const int b = f.commands.createComponent(descriptor("amplifier").create)->graphNodeId();
    store.positions[a] = LayoutPoint{10.0f, 20.0f};
    store.positions[b] = LayoutPoint{300.0f, 40.0f};
    REQUIRE(f.commands.moveNodes({{a, {50.0f, 20.0f}}, {b, {50.0f, 40.0f}}}).empty());

    auto *later = f.commands.createComponent(descriptor("generator").create);
    REQUIRE(later != nullptr);
    const int later_node = later->graphNodeId();

    REQUIRE(f.commands.undo());
    REQUIRE(f.commands.undoLabel() == "Arrange nodes");
    REQUIRE(f.runtime.components().size() == 2);
    bool later_present = false;
    for (const GraphNode &node : f.runtime.graph().nodes())
        later_present = later_present || node.node_id == later_node;
    REQUIRE_FALSE(later_present);
    REQUIRE(store.at(a, 50.0f, 20.0f));

    REQUIRE(f.commands.undo());
    REQUIRE(store.at(a, 10.0f, 20.0f));
    REQUIRE(store.at(b, 300.0f, 40.0f));
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

// Draws one headless frame with a Delete key state and either the Node Editor or another
// window owning focus, so tests can tell a canvas shortcut from a global one.
void drawFrameFocusing(NodeGraphWidget &widget, bool focus_editor, bool delete_down) {
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete, delete_down);
    ImGui::NewFrame();
    if (focus_editor)
        ImGui::SetNextWindowFocus();
    bool open = true;
    widget.draw("Node Editor", &open);
    if (!focus_editor) {
        ImGui::SetNextWindowFocus();
        ImGui::Begin("Other");
        ImGui::End();
    }
    ImGui::EndFrame();
    ImGui::Render();
}

// Draws one headless frame in which a text field in another window takes the keyboard, so the
// next frame starts with io.WantTextInput set.
void drawFrameActivatingTextInput() {
    static char name_buffer[32] = "field";
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete, false);
    ImGui::NewFrame();
    ImGui::SetNextWindowFocus();
    ImGui::Begin("Other");
    ImGui::SetKeyboardFocusHere();
    ImGui::InputText("Name", name_buffer, sizeof(name_buffer));
    ImGui::End();
    ImGui::EndFrame();
    ImGui::Render();
}

// Draws one headless frame in which Delete is down, a text field still holds the keyboard
// (ImGui publishes io.WantTextInput at frame start), and the Node Editor takes focus. A
// canvas-hosted Name field and the Delete that follows it arrive in exactly this order.
void drawFrameTakingFocusFromTextInput(NodeGraphWidget &widget) {
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete, true);
    ImGui::NewFrame();
    REQUIRE(ImGui::GetIO().WantTextInput);
    ImGui::SetNextWindowFocus();
    bool open = true;
    widget.draw("Node Editor", &open);
    ImGui::EndFrame();
    ImGui::Render();
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
    REQUIRE(app.testEditorCommands().undoLabel() == "Arrange nodes");

    REQUIRE(app.testEditorCommands().undo());
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
    const std::size_t records = app.testEditorCommands().undoCount();
    widget.alignSelection(AlignMode::Top);
    REQUIRE(widget.layoutMessage().find("\"Front end\"") != std::string::npos);
    REQUIRE(widget.layoutMessage().find("no longer") == std::string::npos);
    REQUIRE(widget.editorPositionForSave(c).y == 80.0f);
    REQUIRE(app.testEditorCommands().undoCount() == records);
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

    const std::size_t records = app.testEditorCommands().undoCount();
    widget.alignSelection(AlignMode::Top);
    REQUIRE(widget.layoutMessage().find("\"Front end\"") != std::string::npos);
    REQUIRE(widget.editorPositionForSave(c).y == 80.0f);
    REQUIRE(app.testEditorCommands().undoCount() == records);
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
    REQUIRE(app.testEditorCommands().undoLabel() == "Arrange nodes");
    REQUIRE(app.testEditorCommands().canUndo());

    app.newProject();
    REQUIRE_FALSE(app.testEditorCommands().canUndo());
    REQUIRE(app.testEditorCommands().undoCount() == 0);
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

TEST_CASE_METHOD(ImGuiFixture, "Delete removes the selection only while the Node Editor is focused",
                 "[editor_commands][app][delete]") {
    RfSimulatorApp app;
    NodeGraphWidget &widget = app.testGraphWidget();
    const int node = app.testCreateComponent("amplifier", 9701)->graphNodeId();
    // Registers the node in the ImNodes pool, which a selection needs before the first draw.
    widget.setNodeEditorSpacePosition(node, ImVec2(100.0f, 40.0f));
    const size_t before = app.componentCount();
    selectNodes(widget, {node});

    // Another window owns focus: Delete must not reach the canvas.
    drawFrameFocusing(widget, /*focus_editor=*/false, /*delete_down=*/false);
    drawFrameFocusing(widget, false, true);
    CHECK(app.componentCount() == before);

    // The Node Editor owns focus: Delete removes the selection.
    drawFrameFocusing(widget, true, false);
    drawFrameFocusing(widget, true, true);
    CHECK(app.componentCount() == before - 1);
}

TEST_CASE_METHOD(ImGuiFixture, "Delete is ignored while a text field owns the keyboard",
                 "[editor_commands][app][delete]") {
    RfSimulatorApp app;
    NodeGraphWidget &widget = app.testGraphWidget();
    const int node = app.testCreateComponent("amplifier", 9711)->graphNodeId();
    // Registers the node in the ImNodes pool, which a selection needs before the first draw.
    widget.setNodeEditorSpacePosition(node, ImVec2(100.0f, 40.0f));
    const size_t before = app.componentCount();
    selectNodes(widget, {node});

    // The field holds the keyboard when the canvas takes focus and Delete arrives. The first
    // frame requests the field's focus, the second activates it, so the third starts with
    // io.WantTextInput set.
    drawFrameActivatingTextInput();
    drawFrameActivatingTextInput();
    drawFrameTakingFocusFromTextInput(widget);
    CHECK(app.componentCount() == before);
}

namespace {

const GraphNode *findNode(const CircuitRuntime &runtime, int node_id) {
    for (const GraphNode &node : runtime.graph().nodes()) {
        if (node.node_id == node_id)
            return &node;
    }
    return nullptr;
}

std::vector<int> nodePins(const CircuitRuntime &runtime, int node_id, bool outputs) {
    const GraphNode *node = findNode(runtime, node_id);
    return node ? (outputs ? node->output_pin_ids : node->input_pin_ids) : std::vector<int>{};
}

bool hasLink(const CircuitRuntime &runtime, int start_pin, int end_pin) {
    for (const GraphLink &link : runtime.graph().links()) {
        if (link.start_pin_id == start_pin && link.end_pin_id == end_pin)
            return true;
    }
    return false;
}

double gainOf(const CircuitRuntime &runtime, int node_id) {
    const IComponentEngine *component = runtime.components().find(node_id);
    REQUIRE(component != nullptr);
    return component->serialize().at("gain_dB").get<double>();
}

} // namespace

TEST_CASE("Undo and redo a component add", "[editor_commands][undo]") {
    CommandFixture f;
    auto *amplifier = f.commands.createComponent(descriptor("amplifier").create);
    REQUIRE(amplifier != nullptr);
    const int component_id = amplifier->id();
    const int node_id = amplifier->graphNodeId();
    const int input_pin = amplifier->inputPinId();
    const int output_pin = amplifier->outputPinId();
    REQUIRE(f.commands.undoLabel() == "Add Amplifier " + std::to_string(component_id));

    REQUIRE(f.commands.undo());
    REQUIRE(f.runtime.components().size() == 0);
    REQUIRE(findNode(f.runtime, node_id) == nullptr);
    REQUIRE(f.commands.canRedo());
    REQUIRE(f.commands.redoLabel() == "Add Amplifier " + std::to_string(component_id));

    REQUIRE(f.commands.redo());
    const IComponentEngine *restored = f.runtime.components().find(node_id);
    REQUIRE(restored != nullptr);
    CHECK(restored->id() == component_id);
    CHECK(restored->graphNodeId() == node_id);
    CHECK(restored->inputPinId() == input_pin);
    CHECK(restored->outputPinId() == output_pin);
    CHECK(f.runtime.components().size() == 1);
}

TEST_CASE("Undoing a removal restores ids, links, probes, groups and position",
          "[editor_commands][undo]") {
    CommandFixture f;
    PositionStore store;
    store.attach(f.commands);
    auto *generator = f.commands.createComponent(descriptor("generator").create);
    auto *amplifier = f.commands.createComponent(descriptor("amplifier").create);
    auto *mixer = f.commands.createComponent(descriptor("mixer").create);
    REQUIRE(generator != nullptr);
    REQUIRE(amplifier != nullptr);
    REQUIRE(mixer != nullptr);

    const int amplifier_id = amplifier->id();
    const int amplifier_node = amplifier->graphNodeId();
    const std::vector<int> inputs = nodePins(f.runtime, amplifier_node, false);
    const std::vector<int> outputs = nodePins(f.runtime, amplifier_node, true);
    const int generator_output = generator->outputPinId();
    const int mixer_input = nodePins(f.runtime, mixer->graphNodeId(), false).front();
    REQUIRE(f.commands.connect(generator_output, inputs.front()).has_value());
    REQUIRE(f.commands.connect(outputs.front(), mixer_input).has_value());
    REQUIRE(f.commands.addProbePin(outputs.front()));
    const int group = f.commands.createGroup("Chain", {generator->graphNodeId(), amplifier_node});
    REQUIRE(group != -1);
    store.positions[amplifier_node] = LayoutPoint{240.0f, 96.0f};
    const std::vector<int> probes_before = f.runtime.graph().probePins();

    REQUIRE(f.commands.removeComponent(amplifier_node));
    REQUIRE(f.commands.undoLabel() == "Remove Amplifier " + std::to_string(amplifier_id));
    REQUIRE(findNode(f.runtime, amplifier_node) == nullptr);
    REQUIRE(f.runtime.graph().groupById(group) == nullptr);

    REQUIRE(f.commands.undo());
    const IComponentEngine *restored = f.runtime.components().find(amplifier_node);
    REQUIRE(restored != nullptr);
    CHECK(restored->id() == amplifier_id);
    CHECK(restored->graphNodeId() == amplifier_node);
    CHECK(nodePins(f.runtime, amplifier_node, false) == inputs);
    CHECK(nodePins(f.runtime, amplifier_node, true) == outputs);
    CHECK(hasLink(f.runtime, generator_output, inputs.front()));
    CHECK(hasLink(f.runtime, outputs.front(), mixer_input));
    CHECK(f.runtime.graph().probePins() == probes_before);
    const Group *restored_group = f.runtime.graph().groupById(group);
    REQUIRE(restored_group != nullptr);
    CHECK(restored_group->member_node_ids ==
          std::vector<int>{generator->graphNodeId(), amplifier_node});
    CHECK(store.at(amplifier_node, 240.0f, 96.0f));

    REQUIRE(f.commands.redo());
    CHECK(findNode(f.runtime, amplifier_node) == nullptr);
    CHECK(f.runtime.graph().groupById(group) == nullptr);
    CHECK(f.runtime.graph().links().empty());
}

TEST_CASE("Undo and redo connect and disconnect", "[editor_commands][undo]") {
    CommandFixture f;
    auto *generator = f.commands.createComponent(descriptor("generator").create);
    auto *amplifier = f.commands.createComponent(descriptor("amplifier").create);
    const int output = generator->outputPinId();
    const int input = amplifier->inputPinId();

    REQUIRE(f.commands.connect(output, input).has_value());
    REQUIRE(f.commands.undoLabel() == "Connect");
    REQUIRE(f.runtime.graph().links().size() == 1);

    REQUIRE(f.commands.undo());
    REQUIRE(f.runtime.graph().links().empty());
    REQUIRE(amplifier->node().inputs[0] == nullptr);

    REQUIRE(f.commands.redo());
    REQUIRE(f.runtime.graph().links().size() == 1);
    REQUIRE(hasLink(f.runtime, output, input));
    REQUIRE(amplifier->node().inputs[0] == &generator->node().outputs[0]);

    // Redo created a fresh link id, so read the live one before disconnecting.
    const int link_id = f.runtime.graph().links().front().link_id;
    REQUIRE(f.commands.disconnect(link_id));
    REQUIRE(f.commands.undoLabel() == "Disconnect");
    REQUIRE(f.runtime.graph().links().empty());

    REQUIRE(f.commands.undo());
    REQUIRE(hasLink(f.runtime, output, input));
    REQUIRE(amplifier->node().inputs[0] == &generator->node().outputs[0]);
    REQUIRE(f.commands.redo());
    REQUIRE(f.runtime.graph().links().empty());
}

TEST_CASE("Undo a parameter edit restores the serialized state", "[editor_commands][undo]") {
    CommandFixture f;
    auto *amplifier = f.commands.createComponent(descriptor("amplifier").create);
    const int node_id = amplifier->graphNodeId();
    REQUIRE(gainOf(f.runtime, node_id) == 0.0);

    REQUIRE(f.commands.setComponentParams(node_id, {{"gain_dB", 15.0}}).status ==
            ParamWriteStatus::Applied);
    REQUIRE(gainOf(f.runtime, node_id) == 15.0);
    REQUIRE(f.commands.undoLabel() == "Edit Amplifier " + std::to_string(amplifier->id()));

    REQUIRE(f.commands.undo());
    REQUIRE(gainOf(f.runtime, node_id) == 0.0);
}

TEST_CASE("Undo and redo probe add and remove keep slot order", "[editor_commands][undo]") {
    CommandFixture f;
    auto *generator = f.commands.createComponent(descriptor("generator").create);
    auto *amplifier = f.commands.createComponent(descriptor("amplifier").create);
    const int first = generator->outputPinId();
    const int second = amplifier->outputPinId();

    REQUIRE(f.commands.addProbePin(first));
    REQUIRE(f.commands.addProbePin(second));
    REQUIRE(f.commands.undoLabel() == "Add probe");
    REQUIRE(f.runtime.graph().probePins() == std::vector<int>{first, second});

    REQUIRE(f.commands.undo());
    REQUIRE(f.runtime.graph().probePins() == std::vector<int>{first});
    REQUIRE(f.commands.undo());
    REQUIRE(f.runtime.graph().probePins().empty());

    REQUIRE(f.commands.redo());
    REQUIRE(f.commands.redo());
    REQUIRE(f.runtime.graph().probePins() == std::vector<int>{first, second});

    // Removing the first probe leaves the second in slot 0; undo restores both slots.
    REQUIRE(f.commands.removeProbePin(first));
    REQUIRE(f.commands.undoLabel() == "Remove probe");
    REQUIRE(f.runtime.graph().probePins() == std::vector<int>{second});
    REQUIRE(f.commands.undo());
    REQUIRE(f.runtime.graph().probePins() == std::vector<int>{first, second});
    REQUIRE(f.commands.redo());
    REQUIRE(f.runtime.graph().probePins() == std::vector<int>{second});
}

TEST_CASE("Undo group create, rename, collapse and remove", "[editor_commands][undo]") {
    CommandFixture f;
    const int a = f.commands.createComponent(descriptor("generator").create)->graphNodeId();
    const int b = f.commands.createComponent(descriptor("amplifier").create)->graphNodeId();
    const int group = f.commands.createGroup("Chain", {a, b});
    REQUIRE(group != -1);
    REQUIRE(f.commands.undoLabel() == "Create group Chain");
    REQUIRE(f.commands.renameGroup(group, "Front end"));
    REQUIRE(f.commands.undoLabel() == "Rename group");
    REQUIRE(f.commands.setGroupCollapsed(group, false));
    REQUIRE(f.commands.undoLabel() == "Expand group");
    REQUIRE(f.commands.removeGroup(group));
    REQUIRE(f.commands.undoLabel() == "Remove group Front end");
    REQUIRE(f.runtime.graph().groupById(group) == nullptr);

    REQUIRE(f.commands.undo());
    const Group *restored = f.runtime.graph().groupById(group);
    REQUIRE(restored != nullptr);
    CHECK(restored->name == "Front end");
    CHECK(restored->member_node_ids == std::vector<int>{a, b});
    CHECK_FALSE(restored->collapsed);

    REQUIRE(f.commands.undo());
    REQUIRE(f.runtime.graph().groupById(group) != nullptr);
    CHECK(f.runtime.graph().isGroupCollapsed(group));

    REQUIRE(f.commands.undo());
    REQUIRE(f.runtime.graph().groupById(group) != nullptr);
    CHECK(f.runtime.graph().groupById(group)->name == "Chain");

    REQUIRE(f.commands.undo());
    CHECK(f.runtime.graph().groupById(group) == nullptr);
}

TEST_CASE("Undo a group collapse restores members and their positions", "[editor_commands][undo]") {
    CommandFixture f;
    PositionStore store;
    store.attach(f.commands);
    const int a = f.commands.createComponent(descriptor("generator").create)->graphNodeId();
    const int b = f.commands.createComponent(descriptor("amplifier").create)->graphNodeId();
    store.positions[a] = LayoutPoint{10.0f, 20.0f};
    store.positions[b] = LayoutPoint{30.0f, 40.0f};
    const int group = f.commands.createGroup("Front end", {a, b});
    REQUIRE(group != -1);
    REQUIRE(f.commands.setGroupCollapsed(group, false));

    // Collapsing hides the members, but the widget keeps their cached grid positions, so the
    // collapse itself changes no position.
    REQUIRE(f.commands.setGroupCollapsed(group, true));
    REQUIRE(f.runtime.graph().isGroupCollapsed(group));

    REQUIRE(f.commands.undo());
    REQUIRE_FALSE(f.runtime.graph().isGroupCollapsed(group));
    const Group *restored = f.runtime.graph().groupById(group);
    REQUIRE(restored != nullptr);
    CHECK(restored->member_node_ids == std::vector<int>{a, b});
    CHECK(store.at(a, 10.0f, 20.0f));
    CHECK(store.at(b, 30.0f, 40.0f));
}

TEST_CASE("Undo a library part swap restores the predecessor under its ids",
          "[editor_commands][undo]") {
    CommandFixture f;
    ComponentLibrary library;
    library.loadFile(zx60DefinitionPath());
    ComponentLibrary whole_library;
    whole_library.scan(std::string(PROJECT_SOURCE_DIR) + "/component_data/library");
    const auto definitions = library.all();
    REQUIRE(definitions.size() == 1);
    const ComponentDefinition *successor = nullptr;
    for (const ComponentDefinition *definition : whole_library.all()) {
        if (definition && definition->part_number == "AM1143")
            successor = definition;
    }
    REQUIRE(successor != nullptr);

    auto *generator = f.commands.createComponent(descriptor("generator").create);
    const ComponentAddResult placed = f.commands.addLibraryPart(library, *definitions.front());
    REQUIRE(placed.component != nullptr);
    const int predecessor_id = placed.component->id();
    const int predecessor_node = placed.component->graphNodeId();
    const std::vector<int> predecessor_inputs = nodePins(f.runtime, predecessor_node, false);
    const std::vector<int> predecessor_outputs = nodePins(f.runtime, predecessor_node, true);
    const int generator_output = generator->outputPinId();
    REQUIRE(f.commands.connect(generator_output, predecessor_inputs.front()).has_value());

    const ComponentSwapResult swap =
        f.commands.swapLibraryPart(whole_library, *successor, predecessor_node);
    REQUIRE(swap.component != nullptr);
    REQUIRE(swap.new_graph_node_id != predecessor_node);
    REQUIRE(findNode(f.runtime, predecessor_node) == nullptr);
    REQUIRE(hasLink(f.runtime, generator_output,
                    nodePins(f.runtime, swap.new_graph_node_id, false).front()));

    REQUIRE(f.commands.undo());
    REQUIRE(findNode(f.runtime, swap.new_graph_node_id) == nullptr);
    const IComponentEngine *restored = f.runtime.components().find(predecessor_node);
    REQUIRE(restored != nullptr);
    CHECK(restored->id() == predecessor_id);
    CHECK(nodePins(f.runtime, predecessor_node, false) == predecessor_inputs);
    CHECK(nodePins(f.runtime, predecessor_node, true) == predecessor_outputs);
    CHECK(hasLink(f.runtime, generator_output, predecessor_inputs.front()));
    CHECK(f.runtime.graph().links().size() == 1);
}

TEST_CASE("A new edit clears the redo stack", "[editor_commands][undo]") {
    CommandFixture f;
    f.commands.createComponent(descriptor("generator").create);
    REQUIRE(f.commands.undo());
    REQUIRE(f.commands.canRedo());

    f.commands.createComponent(descriptor("amplifier").create);
    REQUIRE_FALSE(f.commands.canRedo());
    REQUIRE(f.commands.redoLabel().empty());
    REQUIRE_FALSE(f.commands.redo());
}

TEST_CASE("Undo and redo never change the runtime epoch", "[editor_commands][undo]") {
    CommandFixture f;
    auto *generator = f.commands.createComponent(descriptor("generator").create);
    auto *amplifier = f.commands.createComponent(descriptor("amplifier").create);
    REQUIRE(f.commands.connect(generator->outputPinId(), amplifier->inputPinId()).has_value());
    const std::uint64_t epoch = f.runtime.epoch();

    REQUIRE(f.commands.removeComponent(generator->graphNodeId()));
    REQUIRE(f.commands.undo());
    REQUIRE(f.commands.redo());

    CHECK(f.runtime.epoch() == epoch);
}

TEST_CASE("An edit group pushes one record labelled by the outermost group",
          "[editor_commands][undo]") {
    CommandFixture f;
    const std::size_t records = f.commands.undoCount();
    f.commands.beginEditGroup("Outer");
    f.commands.createComponent(descriptor("generator").create);
    f.commands.createComponent(descriptor("amplifier").create);
    f.commands.beginEditGroup("Inner");
    REQUIRE_FALSE(f.commands.endEditGroup());
    REQUIRE(f.commands.undoCount() == records);
    // A group is open, so undo must not split it.
    REQUIRE_FALSE(f.commands.undo());

    REQUIRE(f.commands.endEditGroup());
    REQUIRE(f.commands.undoCount() == records + 1);
    REQUIRE(f.commands.undoLabel() == "Outer");

    REQUIRE(f.commands.undo());
    REQUIRE(f.runtime.components().size() == 0);
}

TEST_CASE("Undo leaves a key that changed since the record", "[editor_commands][undo]") {
    CommandFixture f;
    auto *amplifier = f.commands.createComponent(descriptor("amplifier").create);
    const int node_id = amplifier->graphNodeId();

    // A direct runtime write is not a command, so the record still holds the original state.
    f.runtime.components().find(node_id)->deserialize(nlohmann::json{{"gain_dB", 9.0}});
    REQUIRE(gainOf(f.runtime, node_id) == 9.0);

    REQUIRE(f.commands.undo());
    const IComponentEngine *still_there = f.runtime.components().find(node_id);
    REQUIRE(still_there != nullptr);
    CHECK(gainOf(f.runtime, node_id) == 9.0);
    CHECK(f.runtime.components().size() == 1);
}

TEST_CASE("Undo and redo advance the revision and keep the project dirty",
          "[editor_commands][undo]") {
    CommandFixture f;
    auto *amplifier = f.commands.createComponent(descriptor("amplifier").create);
    f.commands.markClean();
    REQUIRE_FALSE(f.commands.isDirty());

    REQUIRE(f.commands.setComponentParams(amplifier->graphNodeId(), {{"gain_dB", 3.0}}).status ==
            ParamWriteStatus::Applied);
    const std::uint64_t edited = f.commands.revision();
    REQUIRE(f.commands.isDirty());

    REQUIRE(f.commands.undo());
    REQUIRE(f.commands.isDirty());
    CHECK(f.commands.revision() > edited);

    REQUIRE(f.commands.redo());
    REQUIRE(f.commands.isDirty());
    CHECK(f.commands.revision() > edited + 1);
}

TEST_CASE_METHOD(ImGuiFixture, "The seeded default circuit is not part of the first undo record",
                 "[editor_commands][app][undo]") {
    RfSimulatorApp app;
    // The app seeds a generator and an amplifier outside the command service.
    REQUIRE(app.componentCount() == 2);
    REQUIRE_FALSE(app.testEditorCommands().canUndo());

    const NodeGraphEngine &graph = app.testGraphEngine();
    int generator_output = -1;
    int amplifier_input = -1;
    for (const GraphNode &node : graph.nodes()) {
        if (node.label.rfind("Generator", 0) == 0)
            generator_output = node.output_pin_ids.front();
        if (node.label.rfind("Amplifier", 0) == 0)
            amplifier_input = node.input_pin_ids.front();
    }
    REQUIRE(generator_output != -1);
    REQUIRE(amplifier_input != -1);

    REQUIRE(app.testEditorCommands().connect(generator_output, amplifier_input).has_value());
    REQUIRE(app.testEditorCommands().undo());

    // Undoing the connect must keep both seeded components and only drop the link.
    CHECK(app.componentCount() == 2);
    CHECK(app.testGraphEngine().links().empty());
}

TEST_CASE("A placed node keeps its position when an unrelated command is undone",
          "[editor_commands][undo]") {
    CommandFixture f;
    PositionStore store;
    store.attach(f.commands);
    auto *generator = f.commands.createComponent(descriptor("generator").create);
    auto *amplifier = f.commands.createComponent(descriptor("amplifier").create);
    const int node = amplifier->graphNodeId();
    // Placed after its add, the way the canvas places a new node: no command records it yet.
    store.positions[node] = LayoutPoint{240.0f, 96.0f};

    REQUIRE(f.commands.connect(generator->outputPinId(), amplifier->inputPinId()).has_value());
    REQUIRE(f.commands.undo());

    // The connect never touched the node, so undoing it must not move the node.
    REQUIRE(f.runtime.components().size() == 2);
    CHECK(store.at(node, 240.0f, 96.0f));
}

TEST_CASE("A grouped double move undoes to the position before the group",
          "[editor_commands][undo]") {
    CommandFixture f;
    PositionStore store;
    store.attach(f.commands);
    const int node = f.commands.createComponent(descriptor("generator").create)->graphNodeId();
    store.positions[node] = LayoutPoint{10.0f, 20.0f};

    f.commands.beginEditGroup("Arrange twice");
    REQUIRE(f.commands.moveNodes({{node, {50.0f, 20.0f}}}).empty());
    REQUIRE(f.commands.moveNodes({{node, {90.0f, 20.0f}}}).empty());
    REQUIRE(f.commands.endEditGroup());
    REQUIRE(store.at(node, 90.0f, 20.0f));

    REQUIRE(f.commands.undo());
    CHECK(store.at(node, 10.0f, 20.0f));
}

TEST_CASE("A node added and arranged inside a group is redone at its arranged position",
          "[editor_commands][undo]") {
    CommandFixture f;
    PositionStore store;
    store.attach(f.commands);

    f.commands.beginEditGroup("Add and arrange");
    auto *added = f.commands.createComponent(descriptor("amplifier").create);
    REQUIRE(added != nullptr);
    const int node = added->graphNodeId();
    REQUIRE(f.commands.moveNodes({{node, {120.0f, 40.0f}}}).empty());
    REQUIRE(f.commands.endEditGroup());
    REQUIRE(store.at(node, 120.0f, 40.0f));

    REQUIRE(f.commands.undo());
    REQUIRE(f.runtime.components().size() == 0);
    // The widget stops reporting a removed node's position until it is drawn again.
    store.positions.erase(node);

    REQUIRE(f.commands.redo());
    REQUIRE(f.runtime.components().size() == 1);
    CHECK(store.at(node, 120.0f, 40.0f));
}

TEST_CASE_METHOD(ImGuiFixture, "A canvas add records its placement for undo and redo",
                 "[editor_commands][app][undo]") {
    RfSimulatorApp app;
    NodeGraphWidget &widget = app.testGraphWidget();
    const std::size_t components = app.componentCount();

    const NodeGraphWidget::AddableComponent *add_generator = nullptr;
    for (const auto &addable : widget.addableComponents()) {
        if (addable.menu_label.find("Generator") != std::string::npos)
            add_generator = &addable;
    }
    REQUIRE(add_generator != nullptr);
    add_generator->on_add(ImVec2(240.0f, 96.0f));
    REQUIRE(app.componentCount() == components + 1);
    REQUIRE(app.testEditorCommands().undoLabel().rfind("Add Generator", 0) == 0);

    int added = -1;
    for (const GraphNode &node : app.testGraphEngine().nodes()) {
        if (node.label.rfind("Generator", 0) == 0 &&
            widget.editorPositionForSave(node.node_id).x == 240.0f) {
            added = node.node_id;
        }
    }
    REQUIRE(added != -1);

    REQUIRE(app.testEditorCommands().undo());
    REQUIRE(app.componentCount() == components);
    REQUIRE(app.testEditorCommands().redo());
    REQUIRE(app.componentCount() == components + 1);
    // The placement is part of the record, so the redone node returns to the canvas spot.
    CHECK(widget.editorPositionForSave(added).x == 240.0f);
    CHECK(widget.editorPositionForSave(added).y == 96.0f);
}

TEST_CASE_METHOD(ImGuiFixture, "Undoing a connect keeps a canvas-placed node in place",
                 "[editor_commands][app][undo]") {
    RfSimulatorApp app;
    NodeGraphWidget &widget = app.testGraphWidget();
    const std::size_t components = app.componentCount();

    const NodeGraphWidget::AddableComponent *add_generator = nullptr;
    for (const auto &addable : widget.addableComponents()) {
        if (addable.menu_label.find("Generator") != std::string::npos)
            add_generator = &addable;
    }
    REQUIRE(add_generator != nullptr);
    add_generator->on_add(ImVec2(240.0f, 96.0f));

    int added = -1;
    int added_output = -1;
    int amplifier_input = -1;
    for (const GraphNode &node : app.testGraphEngine().nodes()) {
        if (node.label.rfind("Generator", 0) == 0 &&
            widget.editorPositionForSave(node.node_id).x == 240.0f) {
            added = node.node_id;
            added_output = node.output_pin_ids.front();
        }
        if (node.label.rfind("Amplifier", 0) == 0)
            amplifier_input = node.input_pin_ids.front();
    }
    REQUIRE(added != -1);
    REQUIRE(added_output != -1);
    REQUIRE(amplifier_input != -1);

    REQUIRE(app.testEditorCommands().connect(added_output, amplifier_input).has_value());
    REQUIRE(app.testEditorCommands().undo());

    // The connect never touched the node, so the undo drops only the link and keeps the spot.
    CHECK(app.componentCount() == components + 1);
    CHECK(app.testGraphEngine().links().empty());
    CHECK(widget.editorPositionForSave(added).x == 240.0f);
    CHECK(widget.editorPositionForSave(added).y == 96.0f);
}

TEST_CASE("A failed undo keeps its record and the next undo retries", "[editor_commands][undo]") {
    CommandFixture f;
    PositionStore store;
    store.attach(f.commands);
    auto *generator = f.commands.createComponent(descriptor("generator").create);
    const int node = generator->graphNodeId();
    store.positions[node] = LayoutPoint{10.0f, 20.0f};

    // One record that adds a component, links it, and moves an existing node, so the undo changes
    // the circuit before the position write fails.
    f.commands.beginEditGroup("Add and move");
    auto *amplifier = f.commands.createComponent(descriptor("amplifier").create);
    REQUIRE(f.commands.connect(generator->outputPinId(), amplifier->inputPinId()).has_value());
    REQUIRE(f.commands.moveNodes({{node, {50.0f, 20.0f}}}).empty());
    REQUIRE(f.commands.endEditGroup());
    const std::size_t undo_records = f.commands.undoCount();
    const std::size_t redo_records = f.commands.redoCount();

    store.fail_next_writes = 1;
    REQUIRE_FALSE(f.commands.undo());
    // The record stays on the stack, ready for a retry.
    REQUIRE(f.commands.undoCount() == undo_records);
    REQUIRE(f.commands.redoCount() == redo_records);
    REQUIRE(f.commands.undoLabel() == "Add and move");
    // The circuit is the partially undone state.
    REQUIRE(f.runtime.components().size() == 1);
    REQUIRE(store.at(node, 50.0f, 20.0f));
    // The baseline matches the actual circuit, so nothing else is re-recorded.
    REQUIRE_FALSE(f.commands.commitEdit("Nothing"));

    // The retry resumes where it stopped and completes the record.
    REQUIRE(f.commands.undo());
    REQUIRE(f.commands.undoCount() == undo_records - 1);
    REQUIRE(store.at(node, 10.0f, 20.0f));
    // The circuit is back to the pre-record state: one component and no link.
    REQUIRE(f.runtime.components().size() == 1);
    REQUIRE(f.runtime.graph().links().empty());
    REQUIRE_FALSE(f.commands.commitEdit("Nothing"));
}

TEST_CASE("A failed redo keeps its record and the next redo retries", "[editor_commands][undo]") {
    CommandFixture f;
    PositionStore store;
    store.attach(f.commands);
    auto *generator = f.commands.createComponent(descriptor("generator").create);
    const int node = generator->graphNodeId();
    store.positions[node] = LayoutPoint{10.0f, 20.0f};
    f.commands.beginEditGroup("Add and move");
    auto *amplifier = f.commands.createComponent(descriptor("amplifier").create);
    REQUIRE(f.commands.connect(generator->outputPinId(), amplifier->inputPinId()).has_value());
    REQUIRE(f.commands.moveNodes({{node, {50.0f, 20.0f}}}).empty());
    REQUIRE(f.commands.endEditGroup());
    REQUIRE(f.commands.undo());
    const std::size_t undo_records = f.commands.undoCount();
    REQUIRE(f.runtime.components().size() == 1);
    REQUIRE(f.runtime.graph().links().empty());

    store.fail_next_writes = 1;
    REQUIRE_FALSE(f.commands.redo());
    REQUIRE(f.commands.undoCount() == undo_records);
    REQUIRE(f.commands.redoLabel() == "Add and move");
    REQUIRE(f.runtime.components().size() == 2);
    REQUIRE(store.at(node, 10.0f, 20.0f));
    REQUIRE_FALSE(f.commands.commitEdit("Nothing"));

    REQUIRE(f.commands.redo());
    REQUIRE(f.commands.undoCount() == undo_records + 1);
    REQUIRE(store.at(node, 50.0f, 20.0f));
    // The circuit is back to the post-record state: both components and the link.
    REQUIRE(f.runtime.components().size() == 2);
    REQUIRE(f.runtime.graph().links().size() == 1);
    REQUIRE_FALSE(f.commands.commitEdit("Nothing"));
}

TEST_CASE("A failed closing call during undo consumes the record", "[editor_commands][undo]") {
    CommandFixture f;
    f.commands.createComponent(descriptor("amplifier").create);
    const std::size_t records = f.commands.undoCount();
    f.commands.onComponentsChanged = []() { throw std::runtime_error("view sync failed"); };

    REQUIRE(f.commands.undo());
    f.commands.onComponentsChanged = nullptr;

    CHECK(f.commands.undoCount() == records - 1);
    CHECK(f.runtime.components().size() == 0);
    // The baseline was recaptured on the failed exit, so nothing is re-recorded.
    CHECK_FALSE(f.commands.commitEdit("Nothing"));
}

TEST_CASE("A failed closing call during redo consumes the record", "[editor_commands][undo]") {
    CommandFixture f;
    f.commands.createComponent(descriptor("amplifier").create);
    REQUIRE(f.commands.undo());
    REQUIRE(f.commands.canRedo());
    f.commands.onComponentsChanged = []() { throw std::runtime_error("view sync failed"); };

    REQUIRE(f.commands.redo());
    f.commands.onComponentsChanged = nullptr;

    CHECK(f.commands.undoCount() == 1);
    CHECK(f.runtime.components().size() == 1);
    CHECK_FALSE(f.commands.commitEdit("Nothing"));
}

TEST_CASE_METHOD(ImGuiFixture, "A swap undo label names the predecessor and the new part",
                 "[editor_commands][app][undo]") {
    RfSimulatorApp app;
    IComponentEngine *amplifier = app.testCreateComponent("amplifier", 9501);
    REQUIRE(amplifier != nullptr);
    const int node = amplifier->graphNodeId();
    const int component_id = amplifier->id();
    std::string old_label;
    for (const GraphNode &graph_node : app.testGraphEngine().nodes()) {
        if (graph_node.node_id == node)
            old_label = graph_node.label;
    }
    REQUIRE_FALSE(old_label.empty());

    ComponentDefinition definition{};
    definition.type = "amplifier";
    definition.part_number = "TEST-ZX60";
    definition.parameters = nlohmann::json{{"gain_dB", 12.0}};
    REQUIRE(app.testSwapLibraryPart(node, definition).empty());
    REQUIRE(app.testEditorCommands().undoLabel() == "Change " + old_label + " to TEST-ZX60");

    REQUIRE(app.testEditorCommands().undo());
    const IComponentEngine *restored = app.testComponents().find(node);
    REQUIRE(restored != nullptr);
    CHECK(restored->id() == component_id);
}

TEST_CASE_METHOD(ImGuiFixture, "A throw inside a grouped app edit leaves the edit group closed",
                 "[editor_commands][app][undo]") {
    RfSimulatorApp app;
    IComponentEngine *amplifier = app.testCreateComponent("amplifier", 9601);
    REQUIRE(amplifier != nullptr);
    // The duplicate path reads the source position from the widget pool.
    app.testGraphWidget().setNodeEditorSpacePosition(amplifier->graphNodeId(),
                                                     ImVec2(100.0f, 40.0f));
    const std::size_t records = app.testEditorCommands().undoCount();
    REQUIRE(app.testEditorCommands().canUndo());

    app.testThrowOnNextDuplicate();
    REQUIRE_THROWS(app.testDuplicateComponent(amplifier->graphNodeId()));

    // The group was closed while the exception unwound.
    REQUIRE(app.testEditorCommands().canUndo());
    auto *generator = app.testCreateComponent("generator", 9602);
    REQUIRE(generator != nullptr);
    REQUIRE(app.testEditorCommands().undoCount() == records + 1);
    REQUIRE(app.testEditorCommands().undo());
}

TEST_CASE("A kept undo still syncs views and advances the revision", "[editor_commands][undo]") {
    CommandFixture f;
    PositionStore store;
    store.attach(f.commands);
    const int node = f.commands.createComponent(descriptor("generator").create)->graphNodeId();
    store.positions[node] = LayoutPoint{10.0f, 20.0f};
    f.commands.beginEditGroup("Add and move");
    f.commands.createComponent(descriptor("amplifier").create);
    REQUIRE(f.commands.moveNodes({{node, {50.0f, 20.0f}}}).empty());
    REQUIRE(f.commands.endEditGroup());
    f.commands.markClean();
    const std::uint64_t revision = f.commands.revision();
    const int views = f.components_changed;
    const std::size_t undo_records = f.commands.undoCount();

    // The component is removed before the position write fails, so this attempt is a kept record
    // over a partially undone circuit.
    store.fail_next_writes = 1;
    REQUIRE_FALSE(f.commands.undo());

    REQUIRE(f.runtime.components().size() == 1);
    REQUIRE(f.commands.undoCount() == undo_records);
    // Everything already applied still reaches the views, the revision, and the dirty flag.
    CHECK(f.components_changed > views);
    CHECK(f.commands.revision() > revision);
    CHECK(f.commands.isDirty());
    CHECK_FALSE(f.commands.commitEdit("Nothing"));
}

TEST_CASE_METHOD(ImGuiFixture,
                 "Undo restores an applied requirements change and keeps the selected mode",
                 "[editor_commands][app][undo]") {
    RfSimulatorApp app;
    ReceiverRequirementsConfig config;
    config.band_start_Hz = 1.0e6;
    config.band_stop_Hz = 2.0e6;
    app.testSetSingleModeRequirements(config);

    ReceiverRequirementsSet &requirements = app.testReceiverRequirements();
    ReceiverRequirementsMode second;
    second.name = "Second";
    second.config = config;
    requirements.modes.push_back(second);
    assignReceiverRequirementsModeIds(requirements);
    REQUIRE(requirements.modes.size() == 2);
    REQUIRE(selectReceiverRequirementsMode(requirements, 1));
    const std::uint32_t selected_id = requirements.modes[1].id;
    app.testEditorCommands().clearHistory();

    requirements.modes[0].name = "Renamed";
    app.testNoteWidgetEdit("Edit requirements");
    app.testFlushWidgetEdit();

    REQUIRE(app.testEditorCommands().undoLabel() == "Edit requirements");
    REQUIRE(app.testEditorCommands().undo());
    REQUIRE(app.testReceiverRequirements().modes[0].name == "Default");
    REQUIRE(app.testReceiverRequirements().selected == 1);
    REQUIRE(app.testReceiverRequirements().modes[1].id == selected_id);
}

TEST_CASE_METHOD(ImGuiFixture, "Undo restores an RF budget edit", "[editor_commands][app][undo]") {
    RfSimulatorApp app;
    app.testEditorCommands().clearHistory();
    const std::size_t stages = app.testRfBudget().stages.size();
    std::string error;
    REQUIRE(addRfBudgetStage(app.testRfBudget(), "amplifier", error));
    REQUIRE(app.testRfBudget().stages.size() == stages + 1);

    app.testNoteWidgetEdit("Edit RF budget");
    app.testFlushWidgetEdit();

    REQUIRE(app.testEditorCommands().undoLabel() == "Edit RF budget");
    REQUIRE(app.testEditorCommands().undo());
    REQUIRE(app.testRfBudget().stages.size() == stages);
}

TEST_CASE_METHOD(ImGuiFixture,
                 "Panning records nothing and undo after a pan restores grid positions",
                 "[editor_commands][app][undo]") {
    RfSimulatorApp app;
    NodeGraphWidget &widget = app.testGraphWidget();
    const int a = app.testCreateComponent("generator", 9801)->graphNodeId();
    const int b = app.testCreateComponent("amplifier", 9802)->graphNodeId();
    ImNodes::EditorContextSet(widget.context());
    ImNodes::EditorContextResetPanning(ImVec2(0.0f, 0.0f));
    widget.setNodeEditorSpacePosition(a, ImVec2(120.0f, 40.0f));
    widget.setNodeEditorSpacePosition(b, ImVec2(320.0f, 80.0f));
    app.testEditorCommands().clearHistory();
    const std::size_t records = app.testEditorCommands().undoCount();

    // Panning moves every editor-space position; grid positions must not follow. A pending edit
    // is noted first, so the flush really runs: the interim editor-space hooks would record the
    // pan's four position changes here.
    ImNodes::EditorContextResetPanning(ImVec2(120.0f, -40.0f));
    REQUIRE(widget.editorPositionForSave(a).x == 240.0f);
    app.testNoteWidgetEdit("Pan probe");
    app.testFlushWidgetEdit();
    REQUIRE(app.testEditorCommands().undoCount() == records);
    REQUIRE(app.testEditorCommands().undoLabel().empty());
    REQUIRE(widget.gridPositionForSave(a).x == 120.0f);
    REQUIRE(widget.gridPositionForSave(a).y == 40.0f);

    widget.setNodeEditorSpacePosition(a, ImVec2(220.0f, 120.0f));
    app.testNoteWidgetEdit("Move nodes");
    app.testFlushWidgetEdit();
    REQUIRE(app.testEditorCommands().undoCount() == records + 1);

    REQUIRE(app.testEditorCommands().undo());
    REQUIRE(widget.gridPositionForSave(a).x == 120.0f);
    REQUIRE(widget.gridPositionForSave(a).y == 40.0f);
    REQUIRE(widget.gridPositionForSave(b).x == 320.0f);
    REQUIRE(widget.gridPositionForSave(b).y == 80.0f);
}

TEST_CASE_METHOD(ImGuiFixture, "A multi-item canvas delete is one undo record",
                 "[editor_commands][app][undo]") {
    RfSimulatorApp app;
    NodeGraphWidget &widget = app.testGraphWidget();
    const int a = app.testCreateComponent("generator", 9811)->graphNodeId();
    const int b = app.testCreateComponent("amplifier", 9812)->graphNodeId();
    const int c = app.testCreateComponent("generator", 9813)->graphNodeId();
    widget.setNodeEditorSpacePosition(a, ImVec2(100.0f, 40.0f));
    widget.setNodeEditorSpacePosition(b, ImVec2(320.0f, 40.0f));
    widget.setNodeEditorSpacePosition(c, ImVec2(100.0f, 200.0f));
    const auto pinsOf = [&app](int node_id, bool outputs) {
        for (const GraphNode &node : app.testGraphEngine().nodes()) {
            if (node.node_id == node_id)
                return outputs ? node.output_pin_ids : node.input_pin_ids;
        }
        return std::vector<int>{};
    };
    const std::vector<int> outputs = pinsOf(a, true);
    const std::vector<int> inputs = pinsOf(b, false);
    REQUIRE_FALSE(outputs.empty());
    REQUIRE_FALSE(inputs.empty());
    const std::optional<int> link_id = app.testConnectLink(outputs.front(), inputs.front());
    REQUIRE(link_id.has_value());
    drawFrame(widget); // registers the three nodes and the link in the pool
    const std::size_t components = app.componentCount();
    const std::size_t records = app.testEditorCommands().undoCount();

    ImNodes::EditorContextSet(widget.context());
    ImNodes::ClearNodeSelection();
    ImNodes::ClearLinkSelection();
    ImNodes::SelectNode(a);
    ImNodes::SelectNode(b);
    ImNodes::SelectNode(c);
    ImNodes::SelectLink(*link_id);
    drawFrameFocusing(widget, /*focus_editor=*/true, /*delete_down=*/false);
    drawFrameFocusing(widget, true, true);

    REQUIRE(app.componentCount() == components - 3);
    REQUIRE(app.testEditorCommands().undoCount() == records + 1);
    REQUIRE(app.testEditorCommands().undoLabel() == "Remove 4 items");

    REQUIRE(app.testEditorCommands().undo());
    REQUIRE(app.componentCount() == components);
    const bool restored_link = std::any_of(
        app.testGraphEngine().links().begin(), app.testGraphEngine().links().end(),
        [&outputs, &inputs](const GraphLink &link) {
            return link.start_pin_id == outputs.front() && link.end_pin_id == inputs.front();
        });
    REQUIRE(restored_link);
}

TEST_CASE_METHOD(ImGuiFixture, "Undoing a PFB removal restores its views",
                 "[editor_commands][app][undo]") {
    RfSimulatorApp app;
    IComponentEngine *pfb = app.testCreateComponent("pfb", 9821);
    REQUIRE(pfb != nullptr);
    const std::size_t views = app.testPfbViewCount();
    const int node_id = pfb->graphNodeId();
    REQUIRE(app.testRemoveComponent(node_id));
    REQUIRE(app.testPfbViewCount() == views - 1);

    REQUIRE(app.testEditorCommands().undo());
    REQUIRE(app.testPfbViewCount() == views);
}

TEST_CASE_METHOD(ImGuiFixture, "New project and agent revert clear the history",
                 "[editor_commands][app][undo]") {
    RfSimulatorApp app;
    app.testCreateComponent("amplifier", 9831);
    REQUIRE(app.testEditorCommands().canUndo());

    app.newProject();
    REQUIRE_FALSE(app.testEditorCommands().canUndo());
    REQUIRE(app.testEditorCommands().undoCount() == 0);

    const AgentToolResult added = app.testAgentCall(
        "circuit_edit",
        nlohmann::ordered_json{
            {"epoch", app.testProjectEpoch()},
            {"ops", nlohmann::ordered_json::array(
                        {{{"op", "add"}, {"ref", "gen"}, {"type", "generator"}}})}});
    REQUIRE_FALSE(added.is_error);
    REQUIRE(app.testAgentHost().checkpoints().size() == 1);
    REQUIRE(app.testEditorCommands().canUndo());

    app.revertAgentCheckpoint(app.testAgentHost().checkpoints().back().id);
    REQUIRE_FALSE(app.testEditorCommands().canUndo());
    REQUIRE(app.testEditorCommands().undoCount() == 0);
}

TEST_CASE_METHOD(ImGuiFixture, "Repeated widget edits before a flush make one record",
                 "[editor_commands][app][undo]") {
    RfSimulatorApp app;
    IComponentEngine *amplifier = app.testCreateComponent("amplifier", 9841);
    REQUIRE(amplifier != nullptr);
    const int node_id = amplifier->graphNodeId();
    app.testEditorCommands().clearHistory();
    const std::size_t records = app.testEditorCommands().undoCount();

    amplifier->deserialize(nlohmann::json{{"gain_dB", 12.0}});
    app.testNoteWidgetEdit("Edit A");
    amplifier->deserialize(nlohmann::json{{"gain_dB", 15.0}});
    app.testNoteWidgetEdit("Edit B");
    app.testFlushWidgetEdit();

    REQUIRE(app.testEditorCommands().undoCount() == records + 1);
    REQUIRE(app.testEditorCommands().undoLabel() == "Edit A");
    const IComponentEngine *restored = app.testComponents().find(node_id);
    REQUIRE(restored != nullptr);
    REQUIRE(restored->serialize().at("gain_dB").get<double>() == 15.0);
}

TEST_CASE_METHOD(ImGuiFixture,
                 "A throwing end-edit-group callback does not escape the canvas delete",
                 "[editor_commands][app][delete]") {
    CircuitRuntime runtime;
    const auto *descriptor = ComponentTypeRegistry::instance().find("amplifier");
    REQUIRE(descriptor != nullptr);
    IComponentEngine *component = runtime.createComponent(descriptor->create);
    REQUIRE(component != nullptr);
    const int node = component->graphNodeId();

    NodeGraphWidgetActions actions;
    int begins = 0;
    int ends = 0;
    actions.removeComponent = [&runtime](int id) { return runtime.removeComponent(id); };
    actions.beginEditGroup = [&begins](std::string) { ++begins; };
    actions.endEditGroup = [&ends]() -> bool {
        ++ends;
        throw std::runtime_error("commit failed");
    };
    NodeGraphWidget widget(runtime.graph(), std::move(actions));
    widget.setNodeEditorSpacePosition(node, ImVec2(100.0f, 40.0f));
    selectNodes(widget, {node});
    drawFrame(widget);
    selectNodes(widget, {node});

    // A throw out of the destructor would terminate the process instead of failing here.
    REQUIRE_NOTHROW(drawFrameFocusing(widget, /*focus_editor=*/true, /*delete_down=*/false));
    REQUIRE_NOTHROW(drawFrameFocusing(widget, true, true));
    REQUIRE(begins == 1);
    // The throw happened after the close was attempted, so a skipped close would report 0 here.
    REQUIRE(ends == 1);
    REQUIRE(runtime.components().size() == 0);
}
