#include "node_graph_engine.h"
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Benchmark: rebuildGroupBoundaryPins with 100 cross-boundary links",
          "[benchmark][group]") {
    NodeGraphEngine engine;

    // 50 group members
    std::vector<SignalNode> member_nodes(50);
    std::vector<int> members;
    for (int i = 0; i < 50; ++i) {
        members.push_back(engine.addNode("N" + std::to_string(i), &member_nodes[i], 1, 1));
    }
    int gid = engine.addGroup("Big", members);

    // 50 output cross-boundary links: each member output -> external input
    std::vector<SignalNode> ext_out_nodes(50);
    for (int i = 0; i < 50; ++i) {
        engine.addNode("E" + std::to_string(i), &ext_out_nodes[i], 1, 0);
        int ext_idx = 50 + i;
        engine.addLink(engine.nodes()[i].output_pin_ids[0],
                       engine.nodes()[ext_idx].input_pin_ids[0]);
    }

    // 50 input cross-boundary links: external output -> each member input
    std::vector<SignalNode> ext_in_nodes(50);
    for (int i = 0; i < 50; ++i) {
        engine.addNode("F" + std::to_string(i), &ext_in_nodes[i], 0, 1);
        int ext_idx = 100 + i;
        engine.addLink(engine.nodes()[ext_idx].output_pin_ids[0],
                       engine.nodes()[i].input_pin_ids[0]);
    }

    BENCHMARK("rebuildGroupBoundaryPins") { engine.rebuildGroupBoundaryPins(gid); };
}

TEST_CASE("Benchmark: group add/remove cycle", "[benchmark][group]") {
    NodeGraphEngine engine;

    std::vector<SignalNode> sig_nodes(10);
    std::vector<int> nodes;
    for (int i = 0; i < 10; ++i) {
        nodes.push_back(engine.addNode("N" + std::to_string(i), &sig_nodes[i], 1, 1));
    }

    BENCHMARK("addGroup+removeGroup") {
        int gid = engine.addGroup("Cycle", nodes);
        engine.removeGroup(gid);
    };
}
