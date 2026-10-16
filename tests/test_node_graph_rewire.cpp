#include "combiner_engine.h"
#include "rewire.h"
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <vector>

TEST_CASE("Rewire clears inputs for empty, lone, and disconnected components",
          "[node_graph][rewire]") {
    SECTION("zero components") {
        NodeGraphEngine graph;
        std::span<IComponentEngine *const> components;
        rewireComponentInputs(components, graph);
    }

    SECTION("one component with unlinked inputs") {
        NodeGraphEngine graph;
        CombinerEngine component(0, graph);
        Spectrum stale;
        component.node().inputs[0] = &stale;
        component.node().inputs[1] = &stale;
        IComponentEngine *components[] = {&component};

        rewireComponentInputs(components, graph);

        REQUIRE(component.node().inputs[0] == nullptr);
        REQUIRE(component.node().inputs[1] == nullptr);
    }

    SECTION("linked source outside the component span still binds for a normal target") {
        NodeGraphEngine graph;
        SignalNode external_source;
        external_source.outputs.resize(1);
        graph.addNode("External source", &external_source, 0, 1);
        CombinerEngine component(0, graph);
        Spectrum stale;
        component.node().inputs[0] = &stale;
        component.node().inputs[1] = &stale;
        graph.addLink(graph.nodes()[0].output_pin_ids[0], component.inputPinId(0));
        IComponentEngine *components[] = {&component};

        rewireComponentInputs(components, graph);

        REQUIRE(component.node().inputs[0] == &external_source.outputs[0]);
        REQUIRE(component.node().inputs[1] == nullptr);
    }
}

TEST_CASE("Rewire binds the first source link into a multiply-linked input",
          "[node_graph][rewire]") {
    NodeGraphEngine graph;
    SignalNode source_a;
    SignalNode source_b;
    source_a.outputs.resize(1);
    source_b.outputs.resize(1);
    graph.addNode("Source A", &source_a, 0, 1);
    graph.addNode("Source B", &source_b, 0, 1);
    CombinerEngine target(0, graph);
    graph.addLink(graph.nodes()[0].output_pin_ids[0], target.inputPinId(0));
    graph.addLink(graph.nodes()[1].output_pin_ids[0], target.inputPinId(0));
    IComponentEngine *components[] = {&target};

    rewireComponentInputs(components, graph);

    REQUIRE(target.node().inputs[0] == &source_a.outputs[0]);
}

TEST_CASE("Rewire skips dangling links before finding a valid source", "[node_graph][rewire]") {
    NodeGraphEngine graph;
    SignalNode source_a;
    source_a.outputs.resize(1);
    graph.addNode("Source A", &source_a, 0, 1);
    CombinerEngine target(0, graph);
    const int target_input = target.inputPinId(0);
    const int dangling_start = 999999;
    graph.addLink(dangling_start, target_input);
    graph.addLink(graph.nodes()[0].output_pin_ids[0], target_input);
    IComponentEngine *components[] = {&target};

    rewireComponentInputs(components, graph);

    REQUIRE(target.node().inputs[0] == &source_a.outputs[0]);
}

struct RewireBenchmarkCircuit {
    NodeGraphEngine graph;
    std::vector<std::unique_ptr<CombinerEngine>> owned;
    std::vector<IComponentEngine *> components;

    explicit RewireBenchmarkCircuit(int count) {
        owned.reserve(static_cast<size_t>(count));
        components.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            owned.push_back(std::make_unique<CombinerEngine>(i, graph));
            components.push_back(owned.back().get());
            if (i > 0) {
                graph.addLink(owned[static_cast<size_t>(i - 1)]->outputPinId(),
                              owned.back()->inputPinId(0));
                graph.addLink(owned[static_cast<size_t>(i - 1)]->outputPinId(),
                              owned.back()->inputPinId(1));
            }
        }
    }
};

TEST_CASE("Benchmark: rewire pass scales with a two-input component chain", "[bench][rewire]") {
    SECTION("25 components") {
        RewireBenchmarkCircuit circuit(25);
        BENCHMARK("rewire 25 components") {
            rewireComponentInputs(circuit.components, circuit.graph);
            return circuit.components.size();
        };
    }
    SECTION("100 components") {
        RewireBenchmarkCircuit circuit(100);
        BENCHMARK("rewire 100 components") {
            rewireComponentInputs(circuit.components, circuit.graph);
            return circuit.components.size();
        };
    }
    SECTION("400 components") {
        RewireBenchmarkCircuit circuit(400);
        BENCHMARK("rewire 400 components") {
            rewireComponentInputs(circuit.components, circuit.graph);
            return circuit.components.size();
        };
    }
}
