#include <catch2/catch_test_macros.hpp>
#include "node_graph_engine.h"

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

    engine.addLink(gen.output_pin_id, amp.input_pin_id);
    REQUIRE(engine.links().size() == 1);

    auto* source = engine.getSourceForInput(amp.input_pin_id);
    REQUIRE(source == &gen_node);
}

TEST_CASE("NodeGraphEngine removes links when node is deleted", "[node_graph]") {
    NodeGraphEngine engine;
    SignalNode gen_node;
    SignalNode amp_node;

    engine.addNode("Generator", &gen_node, false, true);
    engine.addNode("Amplifier", &amp_node, true, true);

    auto& gen = engine.nodes()[0];
    auto& amp = engine.nodes()[1];

    engine.addLink(gen.output_pin_id, amp.input_pin_id);
    engine.removeNode(gen.node_id);

    REQUIRE(engine.links().empty());
    REQUIRE(engine.getSourceForInput(amp.input_pin_id) == nullptr);
}

TEST_CASE("NodeGraphEngine probe management", "[node_graph]") {
    NodeGraphEngine engine;
    SignalNode node;

    engine.addNode("Generator", &node, false, true);
    auto& n = engine.nodes()[0];

    REQUIRE(engine.activeProbePin() == -1);
    REQUIRE(engine.probedSignalNode() == nullptr);

    engine.setActiveProbePin(n.output_pin_id);
    REQUIRE(engine.activeProbePin() == n.output_pin_id);
    REQUIRE(engine.probedSignalNode() == &node);

    engine.removeNode(n.node_id);
    REQUIRE(engine.activeProbePin() == -1);
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

    engine.addLink(g1.output_pin_id, a.input_pin_id);
    engine.addLink(g2.output_pin_id, a.input_pin_id);

    auto sources = engine.getSourcesForInput(a.input_pin_id);
    REQUIRE(sources.size() == 2);
    REQUIRE(sources[0] == &gen1);
    REQUIRE(sources[1] == &gen2);
}
