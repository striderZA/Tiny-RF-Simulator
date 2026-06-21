#include <catch2/catch_test_macros.hpp>
#include "node_graph_engine.h"

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
    const auto& g = engine.groups().front();
    REQUIRE(g.member_node_ids.size() == 2);
    REQUIRE(g.name == "Sub");
    REQUIRE(g.collapsed == false);
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
    REQUIRE_FALSE(engine.isGroupCollapsed(gid));
    engine.setGroupCollapsed(gid, true);
    REQUIRE(engine.isGroupCollapsed(gid));
    engine.setGroupCollapsed(gid, false);
    REQUIRE_FALSE(engine.isGroupCollapsed(gid));
}
