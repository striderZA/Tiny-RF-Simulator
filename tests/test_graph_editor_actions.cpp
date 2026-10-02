#include "circuit_runtime.h"
#include "component_type_registry.h"
#include "graph_editor_actions.h"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

TEST_CASE("Graph editor actions own probe and group mutations without an ImGui context",
          "[graph_editor_actions]") {
    const auto *generator_factory = ComponentTypeRegistry::instance().find("generator");
    const auto *amplifier_factory = ComponentTypeRegistry::instance().find("amplifier");
    REQUIRE(generator_factory != nullptr);
    REQUIRE(amplifier_factory != nullptr);

    CircuitRuntime runtime;
    GraphEditorActions actions(runtime);
    auto *inside = runtime.createComponent(generator_factory->create);
    auto *inside_peer = runtime.createComponent(amplifier_factory->create);
    auto *outside = runtime.createComponent(amplifier_factory->create);
    REQUIRE(inside != nullptr);
    REQUIRE(inside_peer != nullptr);
    REQUIRE(outside != nullptr);
    const auto &graph = runtime.graph();
    const int inside_id = inside->graphNodeId();
    const int inside_peer_id = inside_peer->graphNodeId();

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

    const auto link_id =
        runtime.connect(graph.nodes()[0].output_pin_ids[0], graph.nodes()[2].input_pin_ids[0]);
    REQUIRE(link_id.has_value());
    actions.topologyChanged();
    REQUIRE(graph.groupById(group_id)->boundary_pins.size() == 1);
    REQUIRE(graph.groupById(group_id)->boundary_pins[0].is_output);

    REQUIRE(runtime.disconnect(*link_id));
    actions.topologyChanged();
    REQUIRE(graph.groupById(group_id)->boundary_pins.empty());

    REQUIRE(actions.removeGroup(group_id));
    REQUIRE(graph.groupById(group_id) == nullptr);
}

TEST_CASE("Resetting editor project state clears probes, groups, selection, and counters",
          "[graph_editor_actions]") {
    const auto *generator_factory = ComponentTypeRegistry::instance().find("generator");
    const auto *amplifier_factory = ComponentTypeRegistry::instance().find("amplifier");
    REQUIRE(generator_factory != nullptr);
    REQUIRE(amplifier_factory != nullptr);

    CircuitRuntime runtime;
    GraphEditorActions actions(runtime);
    REQUIRE(runtime.createComponent(generator_factory->create) != nullptr);
    auto *peer = runtime.createComponent(amplifier_factory->create);
    REQUIRE(peer != nullptr);
    const auto &graph = runtime.graph();
    const int node_id = graph.nodes()[0].node_id;
    const int peer_id = peer->graphNodeId();
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
