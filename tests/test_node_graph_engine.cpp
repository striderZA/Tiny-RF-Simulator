#include "node_graph_engine.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>

TEST_CASE("NodeGraphEngine can add and remove nodes", "[node_graph]") {
    NodeGraphEngine engine;
    SignalNode node1;
    SignalNode node2;

    int id1 = engine.addNode("Generator 0", &node1, false, true);
    int id2 = engine.addNode("Amplifier 0", &node2, true, true);

    REQUIRE(engine.nodes().size() == 2);
    REQUIRE(engine.nodes()[0].label == "Generator 0");
    REQUIRE(engine.nodes()[1].label == "Amplifier 0");

    engine.removeNode(id1);
    REQUIRE(engine.nodes().size() == 1);
}

TEST_CASE("NodeGraphEngine can link nodes and query topology", "[node_graph]") {
    NodeGraphEngine engine;
    SignalNode gen_node;
    SignalNode amp_node;

    int gen_id = engine.addNode("Generator", &gen_node, false, true);
    int amp_id = engine.addNode("Amplifier", &amp_node, true, true);

    auto &gen = engine.nodes()[0];
    auto &amp = engine.nodes()[1];

    engine.addLink(gen.output_pin_ids[0], amp.input_pin_ids[0]);
    REQUIRE(engine.links().size() == 1);

    auto source = engine.getSourceForInput(amp.input_pin_ids[0]);
    REQUIRE(source.node == &gen_node);
    REQUIRE(source.output_index == 0);
}

TEST_CASE("NodeGraphEngine removes links when node is deleted", "[node_graph]") {
    NodeGraphEngine engine;
    SignalNode gen_node;
    SignalNode amp_node;

    engine.addNode("Generator", &gen_node, false, true);
    engine.addNode("Amplifier", &amp_node, true, true);

    int gen_pin = engine.nodes()[0].output_pin_ids[0];
    int amp_pin = engine.nodes()[1].input_pin_ids[0];

    engine.addLink(gen_pin, amp_pin);
    engine.removeNode(engine.nodes()[0].node_id);

    REQUIRE(engine.links().empty());
    REQUIRE(engine.getSourceForInput(amp_pin).node == nullptr);
}

TEST_CASE("NodeGraphEngine multi-probe", "[node_graph]") {
    NodeGraphEngine engine;
    SignalNode node1;
    SignalNode node2;

    engine.addNode("Generator", &node1, false, true);
    engine.addNode("Amplifier", &node2, true, true);

    auto first_output = engine.nodes()[0].output_pin_ids[0];
    auto second_output = engine.nodes()[1].output_pin_ids[0];

    // Initially empty
    REQUIRE(engine.probePins().empty());

    // Add probes
    REQUIRE(engine.addProbePin(first_output));
    REQUIRE(engine.probePins().size() == 1);
    REQUIRE(engine.probeSlotForPin(first_output) == 0);

    REQUIRE(engine.addProbePin(second_output));
    REQUIRE(engine.probePins().size() == 2);
    REQUIRE(engine.probeSlotForPin(second_output) == 1);

    // Remove probe
    REQUIRE(engine.removeProbePin(first_output));
    REQUIRE(engine.probePins().size() == 1);
    REQUIRE(engine.probeSlotForPin(first_output) == -1);

    // Cannot re-add same pin
    REQUIRE_FALSE(engine.removeProbePin(999));
    REQUIRE_FALSE(engine.addProbePin(second_output)); // already added
}

TEST_CASE("NodeGraphEngine removeNode clears probes", "[node_graph]") {
    NodeGraphEngine engine;
    SignalNode node;

    engine.addNode("Generator", &node, false, true);
    auto &n = engine.nodes()[0];

    engine.addProbePin(n.output_pin_ids[0]);
    REQUIRE(engine.probePins().size() == 1);

    engine.removeNode(n.node_id);
    REQUIRE(engine.probePins().empty());
}

TEST_CASE("NodeGraphEngine getSourcesForInput returns multiple sources", "[node_graph]") {
    NodeGraphEngine engine;
    SignalNode gen1;
    SignalNode gen2;
    SignalNode amp;

    engine.addNode("Gen1", &gen1, false, true);
    engine.addNode("Gen2", &gen2, false, true);
    engine.addNode("Amp", &amp, true, true);

    auto &g1 = engine.nodes()[0];
    auto &g2 = engine.nodes()[1];
    auto &a = engine.nodes()[2];

    engine.addLink(g1.output_pin_ids[0], a.input_pin_ids[0]);
    engine.addLink(g2.output_pin_ids[0], a.input_pin_ids[0]);

    auto sources = engine.getSourcesForInput(a.input_pin_ids[0]);
    REQUIRE(sources.size() == 2);
    REQUIRE(sources[0].node == &gen1);
    REQUIRE(sources[1].node == &gen2);
}

TEST_CASE("NodeGraphEngine setNextIds controls subsequent IDs", "[node_graph]") {
    NodeGraphEngine engine;
    SignalNode node;

    engine.setNextIds(42, 142, 1042);
    int id = engine.addNode("Test", &node, 1, 1);
    REQUIRE(id == 42);

    const auto &n = engine.nodes()[0];
    REQUIRE(n.input_pin_ids[0] == 142);
    REQUIRE(n.output_pin_ids[0] == 143);

    int link_id = engine.addLink(n.input_pin_ids[0], n.output_pin_ids[0]);
    REQUIRE(link_id == 1042);
}

TEST_CASE("NodeGraphEngine removeAllLinks clears all links", "[node_graph]") {
    NodeGraphEngine engine;
    SignalNode src, dst;

    engine.addNode("Src", &src, false, true);
    engine.addNode("Dst", &dst, true, true);
    auto &g = engine.nodes()[0];
    auto &a = engine.nodes()[1];

    engine.addLink(g.output_pin_ids[0], a.input_pin_ids[0]);
    REQUIRE(engine.links().size() == 1);

    engine.removeAllLinks();
    REQUIRE(engine.links().empty());
}

TEST_CASE("NodeGraphEngine group counter accessors", "[node_graph]") {
    NodeGraphEngine engine;
    SignalNode n1, n2;

    engine.addNode("A", &n1, false, true);
    engine.addNode("B", &n2, true, true);

    // Check default starting values
    REQUIRE(engine.nextGroupId() == 50000);
    REQUIRE(engine.nextBoundaryPinId() == 100000);

    // After adding a group, the counter advances
    int gid = engine.addGroup("g1", {engine.nodes()[0].node_id, engine.nodes()[1].node_id});
    REQUIRE(gid == 50000);
    REQUIRE(engine.nextGroupId() == 50001);

    // setNextGroupId / setNextBoundaryPinId
    engine.setNextGroupId(999);
    REQUIRE(engine.nextGroupId() == 999);

    engine.setNextBoundaryPinId(1999);
    REQUIRE(engine.nextBoundaryPinId() == 1999);
}

TEST_CASE("themeColor returns a non-zero color for every NodeKind", "[node_graph][appearance]") {
    REQUIRE(themeColor(NodeKind::Unknown) != 0u);
    REQUIRE(themeColor(NodeKind::Generator) != 0u);
    REQUIRE(themeColor(NodeKind::Amplifier) != 0u);
    REQUIRE(themeColor(NodeKind::Splitter) != 0u);
    REQUIRE(themeColor(NodeKind::Mixer) != 0u);
    REQUIRE(themeColor(NodeKind::Adc) != 0u);
    REQUIRE(themeColor(NodeKind::PFB) != 0u);
    REQUIRE(themeColor(NodeKind::IdealFilter) != 0u);
    REQUIRE(themeColor(NodeKind::CoaxCable) != 0u);
    REQUIRE(themeColor(NodeKind::GroupCollapsed) != 0u);
    REQUIRE(themeColor(NodeKind::Equalizer) == 0xFF34D399);
}

TEST_CASE("themeColor returns a distinct color per NodeKind", "[node_graph][appearance]") {
    REQUIRE(themeColor(NodeKind::Generator) != themeColor(NodeKind::Amplifier));
    REQUIRE(themeColor(NodeKind::Mixer) != themeColor(NodeKind::Adc));
    REQUIRE(themeColor(NodeKind::PFB) != themeColor(NodeKind::IdealFilter));
    REQUIRE(themeColor(NodeKind::CoaxCable) != themeColor(NodeKind::Unknown));
}
