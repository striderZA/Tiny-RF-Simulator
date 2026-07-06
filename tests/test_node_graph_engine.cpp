#include <catch2/catch_test_macros.hpp>
#include "node_graph_engine.h"
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

    auto& gen = engine.nodes()[0];
    auto& amp = engine.nodes()[1];

    engine.addLink(gen.output_pin_ids[0], amp.input_pin_ids[0]);
    REQUIRE(engine.links().size() == 1);

    auto* source = engine.getSourceForInput(amp.input_pin_ids[0]);
    REQUIRE(source == &gen_node);
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
    REQUIRE(engine.getSourceForInput(amp_pin) == nullptr);
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
    auto& n = engine.nodes()[0];

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

    auto& g1 = engine.nodes()[0];
    auto& g2 = engine.nodes()[1];
    auto& a = engine.nodes()[2];

    engine.addLink(g1.output_pin_ids[0], a.input_pin_ids[0]);
    engine.addLink(g2.output_pin_ids[0], a.input_pin_ids[0]);

    auto sources = engine.getSourcesForInput(a.input_pin_ids[0]);
    REQUIRE(sources.size() == 2);
    REQUIRE(sources[0] == &gen1);
    REQUIRE(sources[1] == &gen2);
}

TEST_CASE("nodeKindFromLabel maps known prefixes", "[node_graph][appearance]") {
    REQUIRE(nodeKindFromLabel("Generator 1") == NodeKind::Generator);
    REQUIRE(nodeKindFromLabel("Amplifier 2") == NodeKind::Amplifier);
    REQUIRE(nodeKindFromLabel("Splitter 3") == NodeKind::Splitter);
    REQUIRE(nodeKindFromLabel("Mixer 4") == NodeKind::Mixer);
    REQUIRE(nodeKindFromLabel("ADC 6") == NodeKind::Adc);
    REQUIRE(nodeKindFromLabel("PFB 7") == NodeKind::PFB);
    REQUIRE(nodeKindFromLabel("IdealFilter 8") == NodeKind::IdealFilter);
    REQUIRE(nodeKindFromLabel("Coax Cable 9") == NodeKind::CoaxCable);
}

TEST_CASE("nodeKindFromLabel returns Unknown for unrecognised input", "[node_graph][appearance]") {
    REQUIRE(nodeKindFromLabel("") == NodeKind::Unknown);
    REQUIRE(nodeKindFromLabel("Subcircuit 1") == NodeKind::Unknown);  // groups handled separately
    REQUIRE(nodeKindFromLabel("generator 1") == NodeKind::Unknown);   // case-sensitive
    REQUIRE(nodeKindFromLabel("Amplifier") == NodeKind::Amplifier);  // no trailing space still matches
}

TEST_CASE("themeColor returns a non-zero color for every NodeKind", "[node_graph][appearance]") {
    REQUIRE(themeColor(NodeKind::Unknown)        != 0u);
    REQUIRE(themeColor(NodeKind::Generator)      != 0u);
    REQUIRE(themeColor(NodeKind::Amplifier)      != 0u);
    REQUIRE(themeColor(NodeKind::Splitter)       != 0u);
    REQUIRE(themeColor(NodeKind::Mixer)          != 0u);
    REQUIRE(themeColor(NodeKind::Adc)            != 0u);
    REQUIRE(themeColor(NodeKind::PFB)            != 0u);
    REQUIRE(themeColor(NodeKind::IdealFilter)    != 0u);
    REQUIRE(themeColor(NodeKind::CoaxCable)      != 0u);
    REQUIRE(themeColor(NodeKind::GroupCollapsed) != 0u);
}

TEST_CASE("themeColor returns a distinct color per NodeKind", "[node_graph][appearance]") {
    REQUIRE(themeColor(NodeKind::Generator) != themeColor(NodeKind::Amplifier));
    REQUIRE(themeColor(NodeKind::Mixer)     != themeColor(NodeKind::Adc));
    REQUIRE(themeColor(NodeKind::PFB)       != themeColor(NodeKind::IdealFilter));
    REQUIRE(themeColor(NodeKind::CoaxCable) != themeColor(NodeKind::Unknown));
}
