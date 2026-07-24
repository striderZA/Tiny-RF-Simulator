#include "component_registry.h"
#include "node_graph_engine.h"
#include "view_manager.h"
#include <catch2/catch_test_macros.hpp>

namespace {
struct TestEngineA : IComponentEngine {
    int m_id;
    int m_graph_id;
    SignalNode m_node;
    TestEngineA(int id, NodeGraphEngine &graph, int extra = 0) : m_id(id), m_graph_id(-1) {
        (void)extra;
        m_graph_id = graph.addNode("TestA", &m_node, 0, 1);
    }
    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_id; }
    int outputPinId() const override { return 1; }
    std::string hoverSummary() const override { return "TestA(" + std::to_string(m_id) + ")"; }
    SignalNode &node() override { return m_node; }
    const SignalNode &node() const override { return m_node; }
    void update(double) override {}
};

struct TestEngineB : IComponentEngine {
    int m_id;
    int m_graph_id;
    SignalNode m_node;
    TestEngineB(int id, NodeGraphEngine &graph) : m_id(id), m_graph_id(-1) {
        m_graph_id = graph.addNode("TestB", &m_node, 1, 1);
    }
    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_id; }
    int outputPinId() const override { return 2; }
    int inputPinId() const override { return 10; }
    std::string hoverSummary() const override { return "TestB(" + std::to_string(m_id) + ")"; }
    SignalNode &node() override { return m_node; }
    const SignalNode &node() const override { return m_node; }
    void update(double) override {}
};
} // namespace

TEST_CASE("ComponentRegistry add and find", "[registry]") {
    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry reg(graph, view);

    auto &a = reg.add<TestEngineA>(1, graph);
    auto &b = reg.add<TestEngineB>(2, graph);

    CHECK(reg.size() == 2);
    CHECK(reg.find(a.graphNodeId()) != nullptr);
    CHECK(reg.find(b.graphNodeId()) != nullptr);
    CHECK(reg.find(999) == nullptr);
}

TEST_CASE("ComponentRegistry remove", "[registry]") {
    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry reg(graph, view);

    auto &a = reg.add<TestEngineA>(1, graph);
    int gid = a.graphNodeId();
    CHECK(reg.size() == 1);

    CHECK(reg.remove(gid));
    CHECK(reg.size() == 0);
    CHECK(reg.find(gid) == nullptr);

    CHECK_FALSE(reg.remove(gid));
}

TEST_CASE("ComponentRegistry hoverSummary", "[registry]") {
    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry reg(graph, view);

    auto &a = reg.add<TestEngineA>(42, graph);
    CHECK(reg.hoverSummary(a.graphNodeId()) == "TestA(42)");
    CHECK(reg.hoverSummary(999).empty());
}

TEST_CASE("ComponentRegistry byType", "[registry]") {
    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry reg(graph, view);

    reg.add<TestEngineA>(1, graph, 100);
    reg.add<TestEngineB>(2, graph);
    reg.add<TestEngineA>(3, graph, 200);

    auto a_engines = reg.byType<TestEngineA>();
    CHECK(a_engines.size() == 2);
    CHECK(a_engines[0]->id() == 1);
    CHECK(a_engines[1]->id() == 3);

    auto b_engines = reg.byType<TestEngineB>();
    CHECK(b_engines.size() == 1);
    CHECK(b_engines[0]->id() == 2);
}

TEST_CASE("ComponentRegistry all() preserves order", "[registry]") {
    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry reg(graph, view);

    reg.add<TestEngineA>(10, graph);
    reg.add<TestEngineB>(20, graph);
    reg.add<TestEngineA>(30, graph);

    auto all = reg.all();
    REQUIRE(all.size() == 3);
    CHECK(all[0]->id() == 10);
    CHECK(all[1]->id() == 20);
    CHECK(all[2]->id() == 30);
}

TEST_CASE("ComponentRegistry remove updates byType", "[registry]") {
    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry reg(graph, view);

    auto &a = reg.add<TestEngineA>(1, graph);
    auto &b = reg.add<TestEngineB>(2, graph);

    CHECK(reg.byType<TestEngineA>().size() == 1);
    reg.remove(a.graphNodeId());
    CHECK(reg.byType<TestEngineA>().empty());
    CHECK(reg.byType<TestEngineB>().size() == 1);

    reg.remove(b.graphNodeId());
    CHECK(reg.byType<TestEngineB>().empty());
    CHECK(reg.size() == 0);
}
