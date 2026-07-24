#include "amplifier_engine.h"
#include "node_graph_engine.h"
#include "signal_generator_engine.h"
#include "splitter_engine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
using Catch::Approx;

TEST_CASE("NodeGraphEngine::addGroup rejects 0 members", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    (void)id_a;
    int id_b = engine.addNode("B", &b, 1, 1);
    (void)id_b;
    REQUIRE(engine.addGroup("Sub", {}) == -1);
    REQUIRE(engine.numGroups() == 0);
}

TEST_CASE("NodeGraphEngine::addGroup rejects 1 member", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    (void)id_a;
    int id_b = engine.addNode("B", &b, 1, 1);
    (void)id_b;
    int gid = engine.addGroup("Sub", {id_a});
    REQUIRE(gid == -1);
    REQUIRE(engine.numGroups() == 0);
}

TEST_CASE("NodeGraphEngine::addGroup with 2 members succeeds", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b});
    REQUIRE(gid >= 50000);
    REQUIRE(gid < 100000);
    REQUIRE(engine.numGroups() == 1);
    const auto &g = engine.groups().front();
    REQUIRE(g.member_node_ids.size() == 2);
    REQUIRE(g.name == "Sub");
    REQUIRE(g.collapsed == true);
}

TEST_CASE("NodeGraphEngine::addGroup rejects overlapping members", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b, c;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int id_c = engine.addNode("C", &c, 1, 1);
    REQUIRE(engine.addGroup("Sub1", {id_a, id_b}) >= 0);
    REQUIRE(engine.addGroup("Sub2", {id_b, id_c}) == -1);
    REQUIRE(engine.numGroups() == 1);
}

TEST_CASE("NodeGraphEngine::addGroup rejects duplicate members", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    REQUIRE(engine.addGroup("Sub", {id_a, id_a}) == -1);
    REQUIRE(engine.addGroup("Sub", {id_a, id_b, id_a}) == -1);
    REQUIRE(engine.numGroups() == 0);
}

TEST_CASE("NodeGraphEngine::addGroup rejects unknown members", "[group]") {
    NodeGraphEngine engine;
    SignalNode a;
    int id_a = engine.addNode("A", &a, 1, 1);
    REQUIRE(engine.addGroup("Sub", {id_a, 9999}) == -1);
    REQUIRE(engine.numGroups() == 0);
}

TEST_CASE("NodeGraphEngine::removeGroup leaves members and links intact", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b, c;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int id_c = engine.addNode("C", &c, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b});
    engine.addLink(engine.nodes()[0].output_pin_ids[0], id_c);

    engine.removeGroup(gid);
    REQUIRE(engine.numGroups() == 0);
    REQUIRE(engine.nodes().size() == 3);
    REQUIRE(engine.links().size() == 1);
    REQUIRE(engine.groupIdForNode(id_a) == -1);
    REQUIRE(engine.groupIdForNode(id_b) == -1);
}

TEST_CASE("NodeGraphEngine::removeGroup clears selection if matching", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b});
    engine.setSelectedGroupId(gid);
    engine.removeGroup(gid);
    REQUIRE(engine.selectedGroupId() == -1);
}

TEST_CASE("NodeGraphEngine::removeGroup is no-op for unknown id", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b});
    engine.removeGroup(9999);
    REQUIRE(engine.numGroups() == 1);
    (void)gid;
}

TEST_CASE("NodeGraphEngine::renameGroup updates name", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int gid = engine.addGroup("Original", {id_a, id_b});
    engine.renameGroup(gid, "Renamed");
    REQUIRE(engine.groups().front().name == "Renamed");
}

TEST_CASE("NodeGraphEngine::setGroupCollapsed flips flag", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b});
    REQUIRE(engine.isGroupCollapsed(gid));
    engine.setGroupCollapsed(gid, false);
    REQUIRE_FALSE(engine.isGroupCollapsed(gid));
    engine.setGroupCollapsed(gid, true);
    REQUIRE(engine.isGroupCollapsed(gid));
}

TEST_CASE("NodeGraphEngine::rebuildGroupBoundaryPins with no cross-boundary links", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    (void)id_a;
    (void)id_b;
    int gid = engine.addGroup("Sub", {id_a, id_b});
    engine.rebuildGroupBoundaryPins(gid);
    REQUIRE(engine.groups().front().boundary_pins.empty());
}

TEST_CASE("NodeGraphEngine::rebuildGroupBoundaryPins synthesizes one pin per cross-boundary link",
          "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b, c;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int id_c = engine.addNode("C", &c, 1, 1);
    (void)id_a;
    (void)id_b;
    (void)id_c;
    int gid = engine.addGroup("Sub", {id_a, id_b});

    // Internal link: A.out -> B.in (both inside the group)
    engine.addLink(engine.nodes()[0].output_pin_ids[0], engine.nodes()[1].input_pin_ids[0]);
    // Cross-boundary link: B.out -> C.in (B is inside, C is outside)
    engine.addLink(engine.nodes()[1].output_pin_ids[0], engine.nodes()[2].input_pin_ids[0]);

    engine.rebuildGroupBoundaryPins(gid);
    const auto &bp = engine.groups().front().boundary_pins;
    REQUIRE(bp.size() == 1);
    REQUIRE(bp[0].is_output == true);
    REQUIRE(bp[0].internal_node_id == id_b);
    REQUIRE(bp[0].internal_pin_id == engine.nodes()[1].output_pin_ids[0]);
    REQUIRE(bp[0].id >= 100000);
    REQUIRE(bp[0].label == "B OUT");
}

TEST_CASE("NodeGraphEngine::rebuildGroupBoundaryPins with input cross-boundary", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b, c;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int id_c = engine.addNode("C", &c, 1, 1);
    (void)id_a;
    (void)id_b;
    (void)id_c;
    int gid = engine.addGroup("Sub", {id_a, id_b});

    // Cross-boundary: C.out -> A.in (C is outside, A is inside)
    engine.addLink(engine.nodes()[2].output_pin_ids[0], engine.nodes()[0].input_pin_ids[0]);

    engine.rebuildGroupBoundaryPins(gid);
    const auto &bp = engine.groups().front().boundary_pins;
    REQUIRE(bp.size() == 1);
    REQUIRE(bp[0].is_output == false);
    REQUIRE(bp[0].internal_node_id == id_a);
    REQUIRE(bp[0].internal_pin_id == engine.nodes()[0].input_pin_ids[0]);
    REQUIRE(bp[0].label == "A IN");
}

TEST_CASE("NodeGraphEngine::rebuildGroupBoundaryPins ignores internal links", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    (void)id_a;
    (void)id_b;
    int gid = engine.addGroup("Sub", {id_a, id_b});
    engine.addLink(engine.nodes()[0].output_pin_ids[0], engine.nodes()[1].input_pin_ids[0]);

    engine.rebuildGroupBoundaryPins(gid);
    REQUIRE(engine.groups().front().boundary_pins.empty());
}

TEST_CASE("NodeGraphEngine::rebuildGroupBoundaryPins unique IDs", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b, c, e1, e2, e3;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int id_c = engine.addNode("C", &c, 1, 1);
    int id_e1 = engine.addNode("E1", &e1, 1, 1);
    int id_e2 = engine.addNode("E2", &e2, 1, 1);
    int id_e3 = engine.addNode("E3", &e3, 1, 1);
    (void)id_a;
    (void)id_b;
    (void)id_c;
    (void)id_e1;
    (void)id_e2;
    (void)id_e3;
    int gid = engine.addGroup("Sub", {id_a, id_b, id_c});

    // Three cross-boundary input links: E1 -> A, E2 -> B, E3 -> C
    engine.addLink(engine.nodes()[3].output_pin_ids[0], engine.nodes()[0].input_pin_ids[0]);
    engine.addLink(engine.nodes()[4].output_pin_ids[0], engine.nodes()[1].input_pin_ids[0]);
    engine.addLink(engine.nodes()[5].output_pin_ids[0], engine.nodes()[2].input_pin_ids[0]);

    engine.rebuildGroupBoundaryPins(gid);
    const auto &bp = engine.groups().front().boundary_pins;
    REQUIRE(bp.size() == 3);
    REQUIRE(bp[0].id != bp[1].id);
    REQUIRE(bp[1].id != bp[2].id);
    REQUIRE(bp[0].id != bp[2].id);
    REQUIRE(bp[0].id >= 100000);
    REQUIRE(bp[1].id >= 100000);
    REQUIRE(bp[2].id >= 100000);
}

TEST_CASE("NodeGraphEngine::removeNode auto-removes group when last member removed", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b});
    (void)gid;

    engine.removeNode(id_a);
    REQUIRE(engine.numGroups() == 0);
}

TEST_CASE("NodeGraphEngine::removeNode auto-removes group when count drops below 2", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b, c;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int id_c = engine.addNode("C", &c, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b, id_c});
    (void)gid;

    engine.removeNode(id_a);
    engine.removeNode(id_b);
    REQUIRE(engine.numGroups() == 0);
}

TEST_CASE("NodeGraphEngine::removeNode leaves group intact when >= 2 members remain", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b, c;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int id_c = engine.addNode("C", &c, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b, id_c});

    engine.removeNode(id_a);
    REQUIRE(engine.numGroups() == 1);
    REQUIRE(engine.groupIdForNode(id_b) == gid);
    REQUIRE(engine.groupIdForNode(id_c) == gid);
}

TEST_CASE("NodeGraphEngine::rebuildGroupBoundaryPins uses per-pin label", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b, c;
    int id_a = engine.addNode("Amp", &a, 1, 1);
    int id_b = engine.addNode("Splitter", &b, 1, 1);
    int id_c = engine.addNode("C", &c, 1, 1);
    (void)id_a;
    (void)id_b;
    (void)id_c;
    int gid = engine.addGroup("Sub", {id_a, id_b});

    // Set per-pin label on the splitter's input
    engine.setNodePinLabels(id_b, {"RF IN"}, {"OUT 1", "OUT 2"});

    // Cross-boundary: C.out -> Splitter.in
    engine.addLink(engine.nodes()[2].output_pin_ids[0], engine.nodes()[1].input_pin_ids[0]);

    engine.rebuildGroupBoundaryPins(gid);
    const auto &bp = engine.groups().front().boundary_pins;
    REQUIRE(bp.size() == 1);
    REQUIRE(bp[0].label == "Splitter RF IN");
}

TEST_CASE("Group preserves signal flow when collapsed", "[group][integration]") {
    NodeGraphEngine engine;
    SignalGeneratorEngine gen(1, engine);
    AmplifierEngine amp(2, engine);
    SplitterEngine splitter(3, engine);

    // Wire: gen -> amp -> splitter
    int gen_out = gen.outputPinId();
    int amp_in = amp.inputPinId();
    int amp_out = amp.outputPinId();
    int split_in = splitter.inputPinId();
    int split_out_0 = splitter.outputPinId(0);
    int split_out_1 = splitter.outputPinId(1);
    (void)split_out_0;
    (void)split_out_1;

    engine.addLink(gen_out, amp_in);
    engine.addLink(amp_out, split_in);

    // Group the amp + splitter
    int gid = engine.addGroup("IF Stage", {amp.graphNodeId(), splitter.graphNodeId()});
    REQUIRE(gid >= 0);

    // Set the generator's tone
    gen.addTone(100e6, -20.0);

    // Wire inputs manually (app::update_dsp does this, but in tests we do it explicitly)
    amp.node().inputs[0] = &gen.node().outputs[0];
    splitter.node().inputs[0] = &amp.node().outputs[0];

    // Run the update loop
    gen.update(0.0);
    amp.update(0.0);
    splitter.update(0.0);

    // The splitter's outputs should match what the amplifier sent in
    REQUIRE(splitter.node().outputs[0].tones.size() == 1);
    REQUIRE(splitter.node().outputs[1].tones.size() == 1);
    REQUIRE(splitter.node().outputs[0].tones[0].freq_Hz == Approx(100e6));
    REQUIRE(splitter.node().outputs[1].tones[0].freq_Hz == Approx(100e6));
}

TEST_CASE("Group preserves topological order", "[group][integration]") {
    NodeGraphEngine engine;
    SignalGeneratorEngine gen(1, engine);
    AmplifierEngine amp(2, engine);
    SplitterEngine splitter(3, engine);

    engine.addLink(gen.outputPinId(), amp.inputPinId());
    engine.addLink(amp.outputPinId(), splitter.inputPinId());

    auto order_before = engine.topologicalOrder();
    int gid = engine.addGroup("IF", {amp.graphNodeId(), splitter.graphNodeId()});
    (void)gid;
    auto order_after = engine.topologicalOrder();

    REQUIRE(order_before == order_after);
}
