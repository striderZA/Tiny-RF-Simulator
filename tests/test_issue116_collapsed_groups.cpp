// Regression tests for GitHub issue #116:
// "Collapsed subcircuit blocks never render" (and three related graph-topology
// edge cases found in the 2026-09-12 codebase audit):
//   1. collapsed-group member positions were dropped with the screen-position
//      cache, so the block bailed before drawing;
//   2. links between two *different* collapsed groups were skipped as if they
//      were internal to one group;
//   3. a second link into one input pin was accepted and then silently ignored
//      by the single-source rewire pass;
//   4. the GUI accepted cycles that the test-flow harness rejects.
#include "amplifier_engine.h"
#include "app.h"
#include "circuit_runtime.h"
#include "component_type_registry.h"
#include "graph_editor_actions.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "node_graph_engine.h"
#include "node_graph_widget.h"
#include "signal_generator_engine.h"
#include "test_temp_paths.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

namespace {

struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
        // A bare ImGui context starts with a (-1,-1) DisplaySize sentinel and a
        // default imgui.ini path; give the widget a real frame to draw into and
        // keep the test run pristine (CWD is the repo root).
        ImGui::GetIO().DisplaySize = ImVec2(1920, 1080);
        ImGui::GetIO().IniFilename = nullptr;
        // No renderer backend here, so the font atlas must be built explicitly
        // (NewFrame() asserts TexIsBuilt when RendererHasTextures is not set).
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

} // namespace

// ---------------------------------------------------------------------------
// 3 & 4 — editing-policy queries on the engine
// ---------------------------------------------------------------------------
TEST_CASE("NodeGraphEngine rejects duplicate input links and cycles", "[issue116][node_graph]") {
    NodeGraphEngine engine;
    SignalNode a, b, c;
    engine.addNode("A", &a, 1, 1);
    engine.addNode("B", &b, 1, 1);
    engine.addNode("C", &c, 1, 1);

    const int a_out = engine.nodes()[0].output_pin_ids[0];
    const int a_in = engine.nodes()[0].input_pin_ids[0];
    const int b_in = engine.nodes()[1].input_pin_ids[0];
    const int b_out = engine.nodes()[1].output_pin_ids[0];
    const int c_in = engine.nodes()[2].input_pin_ids[0];
    const int c_out = engine.nodes()[2].output_pin_ids[0];

    REQUIRE_FALSE(engine.inputHasLink(b_in));
    REQUIRE(engine.canAddLink(a_out, b_in));

    engine.addLink(a_out, b_in);
    REQUIRE(engine.inputHasLink(b_in));

    // A second source into the same input pin is rejected...
    REQUIRE_FALSE(engine.canAddLink(c_out, b_in));
    // ...but that is not a cycle, so the cycle query alone still says no.
    REQUIRE_FALSE(engine.wouldCreateCycle(c_out, b_in));

    // B -> A would close the A -> B cycle.
    REQUIRE(engine.wouldCreateCycle(b_out, a_in));
    REQUIRE_FALSE(engine.canAddLink(b_out, a_in));

    // A link from a node back into its own input is always a cycle.
    REQUIRE(engine.wouldCreateCycle(a_out, a_in));
    REQUIRE_FALSE(engine.canAddLink(a_out, a_in));

    // A fresh, acyclic connection with a free input is allowed.
    REQUIRE(engine.canAddLink(b_out, c_in));
}

// ---------------------------------------------------------------------------
// 1 & 2 — collapsed block rendering and cross-group links
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture,
                 "Issue #116: collapsed blocks render and cross-group links are drawn",
                 "[issue116][widget]") {
    const auto *generator_factory = ComponentTypeRegistry::instance().find("generator");
    const auto *amplifier_factory = ComponentTypeRegistry::instance().find("amplifier");
    REQUIRE(generator_factory != nullptr);
    REQUIRE(amplifier_factory != nullptr);

    CircuitRuntime runtime;
    GraphEditorActions editor_actions(runtime);
    auto *a = runtime.createComponent(generator_factory->create);
    auto *b = runtime.createComponent(amplifier_factory->create);
    auto *c = runtime.createComponent(amplifier_factory->create);
    auto *d = runtime.createComponent(amplifier_factory->create);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    REQUIRE(d != nullptr);
    const auto &graph = runtime.graph();
    const int id_a = a->graphNodeId();
    const int id_b = b->graphNodeId();
    const int id_c = c->graphNodeId();
    const int id_d = d->graphNodeId();

    // Two independent groups, expanded for the first frame so the widget caches
    // their members' positions.
    const int group_a = editor_actions.createGroup("Group A", {id_a, id_b});
    const int group_b = editor_actions.createGroup("Group B", {id_c, id_d});
    // The groups start expanded, so the widget can snapshot member positions.

    // Cross-group link plus an internal link in each group.
    REQUIRE(runtime.connect(graph.nodes()[0].output_pin_ids[0],
                            graph.nodes()[2].input_pin_ids[0])
                .has_value()); // A.out -> C.in
    REQUIRE(runtime.connect(graph.nodes()[0].output_pin_ids[0],
                            graph.nodes()[1].input_pin_ids[0])
                .has_value()); // A.out -> B.in (internal)
    REQUIRE(runtime.connect(graph.nodes()[2].output_pin_ids[0],
                            graph.nodes()[3].input_pin_ids[0])
                .has_value()); // C.out -> D.in (internal)
    editor_actions.topologyChanged();

    const NodeGraphEngine &graph_view = graph;
    NodeGraphWidgetActions actions;
    actions.connectLink = [&](int start_pin_id, int end_pin_id) -> std::optional<int> {
        auto link_id = runtime.connect(start_pin_id, end_pin_id);
        if (link_id)
            editor_actions.topologyChanged();
        return link_id;
    };
    actions.disconnectLink = [&](int link_id) {
        const bool disconnected = runtime.disconnect(link_id);
        if (disconnected)
            editor_actions.topologyChanged();
        return disconnected;
    };
    actions.createGroup = [&](std::string name, std::vector<int> members) {
        return editor_actions.createGroup(std::move(name), std::move(members));
    };
    actions.removeGroup = [&](int group_id) { return editor_actions.removeGroup(group_id); };
    actions.renameGroup = [&](int group_id, std::string name) {
        return editor_actions.renameGroup(group_id, std::move(name));
    };
    actions.setGroupCollapsed = [&](int group_id, bool collapsed) {
        return editor_actions.setGroupCollapsed(group_id, collapsed);
    };
    actions.selectGroup = [&](int group_id) { editor_actions.selectGroup(group_id); };
    actions.setGroupCollapsed(group_a, false);
    actions.setGroupCollapsed(group_b, false);
    NodeGraphWidget widget(graph_view, actions);
    bool open = true;

    ImNodes::EditorContextSet(widget.context());
    ImNodes::SetNodeEditorSpacePos(id_a, ImVec2(0, 0));
    ImNodes::SetNodeEditorSpacePos(id_b, ImVec2(200, 0));
    ImNodes::SetNodeEditorSpacePos(id_c, ImVec2(0, 300));
    ImNodes::SetNodeEditorSpacePos(id_d, ImVec2(200, 300));

    // Frame 1: expanded groups render their members and populate the widget's
    // position cache.
    ImGui::NewFrame();
    widget.draw("Node Editor Test", &open);
    REQUIRE(open);
    ImGui::EndFrame();

    // Collapse both groups; the members are no longer drawn.
    actions.setGroupCollapsed(group_a, true);
    actions.setGroupCollapsed(group_b, true);

    // Frame 2: both blocks must render from the cached positions, and the
    // A.out -> C.in link must be drawn through both groups' boundary pins.
    ImGui::NewFrame();
    widget.draw("Node Editor Test", &open);
    REQUIRE(open);
    ImGui::EndFrame();

    REQUIRE(widget.collapsedGroupBlockRendered(group_a));
    REQUIRE(widget.collapsedGroupBlockRendered(group_b));
    REQUIRE(widget.crossGroupLinksDrawn() == 1);

    // The boundary pins the block renders are the engine's cross-boundary pins.
    REQUIRE(graph.groupById(group_a)->boundary_pins.size() == 1);
    REQUIRE(graph.groupById(group_a)->boundary_pins[0].is_output);
    REQUIRE(graph.groupById(group_b)->boundary_pins.size() == 1);
    REQUIRE_FALSE(graph.groupById(group_b)->boundary_pins[0].is_output);
}

TEST_CASE_METHOD(
    ImGuiFixture,
    "Issue #116: widget link requests commit only through callbacks and refresh boundaries",
    "[issue116][widget][actions]") {
    const auto *generator_factory = ComponentTypeRegistry::instance().find("generator");
    const auto *amplifier_factory = ComponentTypeRegistry::instance().find("amplifier");
    REQUIRE(generator_factory != nullptr);
    REQUIRE(amplifier_factory != nullptr);

    CircuitRuntime runtime;
    GraphEditorActions editor_actions(runtime);
    auto *a = runtime.createComponent(generator_factory->create);
    auto *b = runtime.createComponent(amplifier_factory->create);
    auto *c = runtime.createComponent(amplifier_factory->create);
    auto *d = runtime.createComponent(amplifier_factory->create);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    REQUIRE(d != nullptr);
    const auto &graph = runtime.graph();
    const int node_a = a->graphNodeId();
    const int node_b = b->graphNodeId();
    const int node_c = c->graphNodeId();
    const int node_d = d->graphNodeId();
    const int group_a = editor_actions.createGroup("A group", {node_a, node_b});
    const int group_b = editor_actions.createGroup("B group", {node_c, node_d});
    NodeGraphWidgetActions actions;
    actions.connectLink = [&](int start_pin_id, int end_pin_id) -> std::optional<int> {
        auto link_id = runtime.connect(start_pin_id, end_pin_id);
        if (link_id)
            editor_actions.topologyChanged();
        return link_id;
    };
    actions.disconnectLink = [&](int link_id) {
        const bool disconnected = runtime.disconnect(link_id);
        if (disconnected)
            editor_actions.topologyChanged();
        return disconnected;
    };
    const NodeGraphEngine &graph_view = graph;
    NodeGraphWidget widget(graph_view, actions);
    (void)widget;

    const int accepted_link =
        *actions.connectLink(graph.nodes()[0].output_pin_ids[0], graph.nodes()[2].input_pin_ids[0]);
    REQUIRE(graph.links().size() == 1);
    REQUIRE(graph.groupById(group_a)->boundary_pins.size() == 1);
    REQUIRE(graph.groupById(group_a)->boundary_pins[0].is_output);
    REQUIRE(graph.groupById(group_b)->boundary_pins.size() == 1);
    REQUIRE_FALSE(graph.groupById(group_b)->boundary_pins[0].is_output);

    REQUIRE_FALSE(
        actions.connectLink(graph.nodes()[1].output_pin_ids[0], graph.nodes()[2].input_pin_ids[0]));
    REQUIRE(graph.links().size() == 1);

    REQUIRE(actions.disconnectLink(accepted_link));
    REQUIRE(graph.links().empty());
    REQUIRE(graph.groupById(group_a)->boundary_pins.empty());
    REQUIRE(graph.groupById(group_b)->boundary_pins.empty());
}

// ---------------------------------------------------------------------------
// 3 & 4 — the app's link-creation callback enforces the policy
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture,
                 "Issue #116: app rejects duplicate-input and cyclic links at creation",
                 "[issue116][app]") {
    RfSimulatorApp app;
    app.newProject();

    auto &gen = static_cast<SignalGeneratorEngine &>(*app.testCreateComponent("generator", 10001));
    auto &gen2 = static_cast<SignalGeneratorEngine &>(*app.testCreateComponent("generator", 10002));
    auto &amp1 = static_cast<AmplifierEngine &>(*app.testCreateComponent("amplifier", 10003));

    // First link into amp1's input is accepted and committed.
    REQUIRE(app.testConnectLink(gen.outputPinId(), amp1.inputPinId()).has_value());

    // A second source into the occupied input is rejected.
    REQUIRE_FALSE(app.testConnectLink(gen2.outputPinId(), amp1.inputPinId()).has_value());

    // Cycle: amp3 -> amp4 then amp4 -> amp3.
    auto &amp3 = static_cast<AmplifierEngine &>(*app.testCreateComponent("amplifier", 10004));
    auto &amp4 = static_cast<AmplifierEngine &>(*app.testCreateComponent("amplifier", 10005));
    REQUIRE(app.testConnectLink(amp3.outputPinId(), amp4.inputPinId()).has_value());
    REQUIRE_FALSE(app.testConnectLink(amp4.outputPinId(), amp3.inputPinId()).has_value());

    // Control: a fresh acyclic link with a free input is accepted.
    REQUIRE(app.testConnectLink(amp1.outputPinId(), amp3.inputPinId()).has_value());
}

// ---------------------------------------------------------------------------
// 3 & 4 — a hand-edited project cannot smuggle in the same invalid links
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Issue #116: project load drops duplicate-input and cyclic links",
                 "[issue116][project]") {
    // Absolute temp path: this standalone executable runs with the source tree
    // as its CTest working directory, so a relative name would write into it.
    const std::string path =
        (std::filesystem::temp_directory_path() /
         ("test_issue116_invalid_links_" + test_temp_paths::processTag() + ".rfsim"))
            .string();
    std::remove(path.c_str());
    {
        std::ofstream out(path);
        out << R"json({
            "version": 1,
            "components": [
                {"type": "SignalGenerator", "params": {}, "pos": {"x": 0, "y": 0}},
                {"type": "SignalGenerator", "params": {}, "pos": {"x": 0, "y": 100}},
                {"type": "Amplifier", "params": {}, "pos": {"x": 200, "y": 0}},
                {"type": "Amplifier", "params": {}, "pos": {"x": 400, "y": 0}},
                {"type": "Amplifier", "params": {}, "pos": {"x": 600, "y": 0}}
            ],
            "links": [
                {"from": 0, "from_port": 0, "to": 2, "to_port": 0},
                {"from": 1, "from_port": 0, "to": 2, "to_port": 0},
                {"from": 3, "from_port": 0, "to": 4, "to_port": 0},
                {"from": 4, "from_port": 0, "to": 3, "to_port": 0}
            ],
            "probe_pins": [],
            "groups": [],
            "network_analyzer": {},
            "window_state": {},
            "graph_state": {}
        })json";
    }

    RfSimulatorApp app;
    app.loadProject(path);
    REQUIRE(app.componentCount() == 5);
    // The duplicate-input link (gen1 -> ampA) and the cycle-closing link
    // (ampC -> ampB) are both dropped; the two valid links survive.
    REQUIRE(app.testGraphEngine().links().size() == 2);
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// 1 — a project loaded with a collapsed group renders its block on the first
// frame, from the grid-position snapshot ProjectSerializer takes before the
// members can be dropped from the imnodes pool
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture,
                 "Issue #116: a loaded collapsed group renders from the loader snapshot",
                 "[issue116][project]") {
    // Absolute temp path: this standalone executable runs with the source tree
    // as its CTest working directory, so a relative name would write into it.
    const std::string path =
        (std::filesystem::temp_directory_path() /
         ("test_issue116_collapsed_load_" + test_temp_paths::processTag() + ".rfsim"))
            .string();
    std::remove(path.c_str());
    {
        std::ofstream out(path);
        out << R"json({
            "version": 1,
            "components": [
                {"type": "SignalGenerator", "params": {}, "pos": {"x": 0, "y": 0}},
                {"type": "Amplifier", "params": {}, "pos": {"x": 0, "y": 100}},
                {"type": "Amplifier", "params": {}, "pos": {"x": 200, "y": 0}}
            ],
            "links": [
                {"from": 0, "from_port": 0, "to": 1, "to_port": 0},
                {"from": 1, "from_port": 0, "to": 2, "to_port": 0}
            ],
            "probe_pins": [],
            "groups": [{"name": "Collapsed", "member_components": [0, 1], "collapsed": true}],
            "network_analyzer": {},
            "window_state": {},
            "graph_state": {}
        })json";
    }

    RfSimulatorApp app;
    app.loadProject(path);
    REQUIRE(app.componentCount() == 3);
    REQUIRE(app.testGraphEngine().numGroups() == 1);
    const int group_id = app.testGraphEngine().groups()[0].id;
    REQUIRE(app.testGraphEngine().groups()[0].collapsed);
    const auto *group = app.testGraphEngine().groupById(group_id);
    REQUIRE(group != nullptr);
    // The member -> external link is the block's single output boundary pin.
    REQUIRE(group->boundary_pins.size() == 1);
    REQUIRE(group->boundary_pins[0].is_output);

    // The members were never drawn, so only the loader's captureGridPositions()
    // snapshot can place the block on this first frame.
    bool open = true;
    ImGui::NewFrame();
    app.testGraphWidget().draw("Collapsed Load Test", &open);
    ImGui::EndFrame();

    REQUIRE(open);
    REQUIRE(app.testGraphWidget().collapsedGroupBlockRendered(group_id));
    std::remove(path.c_str());
}
