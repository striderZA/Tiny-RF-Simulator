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
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "node_graph_engine.h"
#include "node_graph_widget.h"
#include "signal_generator_engine.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <fstream>
#include <string>

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
    NodeGraphEngine graph;
    SignalNode a, b, c, d;
    const int id_a = graph.addNode("A", &a, 1, 1);
    const int id_b = graph.addNode("B", &b, 1, 1);
    const int id_c = graph.addNode("C", &c, 1, 1);
    const int id_d = graph.addNode("D", &d, 1, 1);

    // Two independent groups, expanded for the first frame so the widget caches
    // their members' positions.
    const int group_a = graph.addGroup("Group A", {id_a, id_b});
    const int group_b = graph.addGroup("Group B", {id_c, id_d});
    graph.setGroupCollapsed(group_a, false);
    graph.setGroupCollapsed(group_b, false);

    // Cross-group link plus an internal link in each group.
    graph.addLink(graph.nodes()[0].output_pin_ids[0],
                  graph.nodes()[2].input_pin_ids[0]); // A.out -> C.in
    graph.addLink(graph.nodes()[0].output_pin_ids[0],
                  graph.nodes()[1].input_pin_ids[0]); // A.out -> B.in (internal)
    graph.addLink(graph.nodes()[2].output_pin_ids[0],
                  graph.nodes()[3].input_pin_ids[0]); // C.out -> D.in (internal)

    NodeGraphWidget widget(graph);
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
    graph.setGroupCollapsed(group_a, true);
    graph.setGroupCollapsed(group_b, true);
    graph.rebuildGroupBoundaryPins(group_a);
    graph.rebuildGroupBoundaryPins(group_b);

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

// ---------------------------------------------------------------------------
// 3 & 4 — the app's link-creation callback enforces the policy
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture,
                 "Issue #116: app rejects duplicate-input and cyclic links at creation",
                 "[issue116][app]") {
    RfSimulatorApp app;
    app.newProject();

    auto &gen = app.testComponents().add<SignalGeneratorEngine>(10001, app.testGraphEngine());
    auto &gen2 = app.testComponents().add<SignalGeneratorEngine>(10002, app.testGraphEngine());
    auto &amp1 = app.testComponents().add<AmplifierEngine>(10003, app.testGraphEngine());

    REQUIRE(app.testGraphWidget().onLinkCreating);

    // First link into amp1's input is accepted and committed.
    REQUIRE(app.testGraphWidget().onLinkCreating(gen.outputPinId(), amp1.inputPinId()));
    app.testGraphEngine().addLink(gen.outputPinId(), amp1.inputPinId());

    // A second source into the occupied input is rejected.
    REQUIRE_FALSE(app.testGraphWidget().onLinkCreating(gen2.outputPinId(), amp1.inputPinId()));

    // Cycle: amp3 -> amp4 then amp4 -> amp3.
    auto &amp3 = app.testComponents().add<AmplifierEngine>(10004, app.testGraphEngine());
    auto &amp4 = app.testComponents().add<AmplifierEngine>(10005, app.testGraphEngine());
    REQUIRE(app.testGraphWidget().onLinkCreating(amp3.outputPinId(), amp4.inputPinId()));
    app.testGraphEngine().addLink(amp3.outputPinId(), amp4.inputPinId());
    REQUIRE_FALSE(app.testGraphWidget().onLinkCreating(amp4.outputPinId(), amp3.inputPinId()));

    // Control: a fresh acyclic link with a free input is accepted.
    REQUIRE(app.testGraphWidget().onLinkCreating(amp1.outputPinId(), amp3.inputPinId()));
}

// ---------------------------------------------------------------------------
// 3 & 4 — a hand-edited project cannot smuggle in the same invalid links
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Issue #116: project load drops duplicate-input and cyclic links",
                 "[issue116][project]") {
    const std::string path = "test_issue116_invalid_links.rfsim";
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
    const std::string path = "test_issue116_collapsed_load.rfsim";
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
