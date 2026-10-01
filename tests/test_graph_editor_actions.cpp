#include "graph_editor_actions.h"
#include "node_graph_engine.h"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

TEST_CASE("Graph editor actions own probe and group mutations without an ImGui context",
          "[graph_editor_actions]") {
    NodeGraphEngine graph;
    SignalNode inside, inside_peer, outside;
    const int inside_id = graph.addNode("Inside", &inside, 1, 1);
    const int inside_peer_id = graph.addNode("Inside peer", &inside_peer, 1, 1);
    graph.addNode("Outside", &outside, 1, 1);
    GraphEditorActions actions(graph);

    const int output_pin = graph.nodes()[0].output_pin_ids[0];
    REQUIRE(actions.addProbePin(output_pin));
    REQUIRE(graph.probePins() == std::vector<int>{output_pin});
    REQUIRE_FALSE(actions.addProbePin(output_pin));
    REQUIRE(actions.removeProbePin(output_pin));
    REQUIRE(graph.probePins().empty());
    REQUIRE(actions.addProbePin(output_pin));
    actions.clearProbes();
    REQUIRE(graph.probePins().empty());

    const int group_id = actions.createGroup("Original", {inside_id, inside_peer_id});
    REQUIRE(graph.groupById(group_id) != nullptr);
    REQUIRE(actions.renameGroup(group_id, "Renamed"));
    REQUIRE(graph.groupById(group_id)->name == "Renamed");
    REQUIRE(actions.setGroupCollapsed(group_id, true));
    REQUIRE(graph.groupById(group_id)->collapsed);
    actions.selectGroup(group_id);
    REQUIRE(graph.selectedGroupId() == group_id);

    actions.setNodePartNumber(inside_id, "PN-42");
    REQUIRE(graph.nodes()[0].part_number == "PN-42");

    const int link_id =
        graph.addLink(graph.nodes()[0].output_pin_ids[0], graph.nodes()[2].input_pin_ids[0]);
    (void)link_id;
    actions.topologyChanged();
    REQUIRE(graph.groupById(group_id)->boundary_pins.size() == 1);
    REQUIRE(graph.groupById(group_id)->boundary_pins[0].is_output);

    graph.removeAllLinks();
    actions.topologyChanged();
    REQUIRE(graph.groupById(group_id)->boundary_pins.empty());

    REQUIRE(actions.removeGroup(group_id));
    REQUIRE(graph.groupById(group_id) == nullptr);
}

TEST_CASE("Resetting editor project state clears probes, groups, selection, and counters",
          "[graph_editor_actions]") {
    NodeGraphEngine graph;
    SignalNode node, peer;
    const int node_id = graph.addNode("Node", &node, 1, 1);
    const int peer_id = graph.addNode("Peer", &peer, 1, 1);
    GraphEditorActions actions(graph);
    const int group_id = actions.createGroup("Temporary", {node_id, peer_id});
    REQUIRE(actions.addProbePin(graph.nodes()[0].output_pin_ids[0]));
    actions.selectGroup(group_id);

    actions.resetForProjectReplacement();

    REQUIRE(graph.probePins().empty());
    REQUIRE(graph.groups().empty());
    REQUIRE(graph.selectedGroupId() == -1);
    REQUIRE(graph.nextGroupId() == 50000);
    REQUIRE(graph.nextBoundaryPinId() == 100000);
}
