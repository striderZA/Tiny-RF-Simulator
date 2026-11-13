#include "circuit_runtime.h"
#include "component_type_registry.h"
#include "graph_editor_actions.h"
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

const ComponentTypeDescriptor &descriptor(std::string_view type) {
    const auto *desc = ComponentTypeRegistry::instance().find(type);
    REQUIRE(desc != nullptr);
    return *desc;
}

const GraphNode *findNode(const CircuitRuntime &runtime, int node_id) {
    for (const GraphNode &node : runtime.graph().nodes()) {
        if (node.node_id == node_id)
            return &node;
    }
    return nullptr;
}

} // namespace

TEST_CASE("restoreComponent rebuilds a removed component under its original ids",
          "[editor_history][restore]") {
    CircuitRuntime runtime;

    auto *generator = runtime.createComponent(descriptor("generator").create);
    auto *amplifier = runtime.createComponent(descriptor("amplifier").create);
    REQUIRE(generator != nullptr);
    REQUIRE(amplifier != nullptr);

    const int component_id = amplifier->id();
    const int graph_node_id = amplifier->graphNodeId();
    const GraphNode *before = findNode(runtime, graph_node_id);
    REQUIRE(before != nullptr);
    const std::vector<int> input_pins = before->input_pin_ids;
    const std::vector<int> output_pins = before->output_pin_ids;
    REQUIRE_FALSE(input_pins.empty());
    REQUIRE_FALSE(output_pins.empty());

    REQUIRE(runtime.removeComponent(graph_node_id));
    // Advance every counter past the removed component, so restoring is the
    // only way the original ids come back.
    REQUIRE(runtime.createComponent(descriptor("mixer").create) != nullptr);

    const int next_node_before = runtime.graph().nextNodeId();
    const int next_pin_before = runtime.graph().nextPinId();

    IComponentEngine *restored = runtime.restoreComponent(
        descriptor("amplifier").create, component_id, graph_node_id, input_pins, output_pins);

    REQUIRE(restored != nullptr);
    CHECK(restored->id() == component_id);
    CHECK(restored->graphNodeId() == graph_node_id);
    const GraphNode *after = findNode(runtime, graph_node_id);
    REQUIRE(after != nullptr);
    CHECK(after->input_pin_ids == input_pins);
    CHECK(after->output_pin_ids == output_pins);
    CHECK(runtime.graph().nextNodeId() == next_node_before);
    CHECK(runtime.graph().nextPinId() == next_pin_before);
}

TEST_CASE("restoreComponent refuses a live id", "[editor_history][restore]") {
    CircuitRuntime runtime;

    auto *generator = runtime.createComponent(descriptor("generator").create);
    REQUIRE(generator != nullptr);

    const std::size_t count_before = runtime.components().size();
    const int next_component_before = runtime.nextComponentId();
    const int next_node_before = runtime.graph().nextNodeId();
    const int next_pin_before = runtime.graph().nextPinId();

    IComponentEngine *restored =
        runtime.restoreComponent(descriptor("amplifier").create, runtime.nextComponentId(),
                                 generator->graphNodeId(), {}, {});

    CHECK(restored == nullptr);
    CHECK(runtime.components().size() == count_before);
    CHECK(runtime.nextComponentId() == next_component_before);
    CHECK(runtime.graph().nextNodeId() == next_node_before);
    CHECK(runtime.graph().nextPinId() == next_pin_before);
}
