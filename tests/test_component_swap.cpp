// Issue #200: in-place replacement of a placed library component. Each case builds its own
// runtime, so component ids, revisions, and the graph start from the same state every time.
#include "circuit_runtime.h"
#include "component_library.h"
#include "component_type_registry.h"
#include "editor_commands.h"
#include "graph_editor_actions.h"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

const ComponentDefinition *findPart(const ComponentLibrary &library,
                                    const std::string &part_number) {
    for (const ComponentDefinition *definition : library.all()) {
        if (definition && definition->part_number == part_number)
            return definition;
    }
    return nullptr;
}

struct SwapFixture {
    CircuitRuntime runtime;
    GraphEditorActions actions{runtime};
    EditorCommands commands{runtime, actions};
    ComponentLibrary library;

    SwapFixture() { library.scan(std::string(PROJECT_SOURCE_DIR) + "/component_data/library"); }

    IComponentEngine *add(std::string_view type) {
        const auto *descriptor = ComponentTypeRegistry::instance().find(type);
        return descriptor ? commands.createComponent(descriptor->create) : nullptr;
    }

    IComponentEngine *addPart(const std::string &part_number) {
        const ComponentDefinition *definition = findPart(library, part_number);
        return definition ? commands.addLibraryPart(library, *definition).component : nullptr;
    }

    // Graph pin id of the `index`-th input or output port of a live node.
    int pin(int node_id, bool output, size_t index) const {
        for (const GraphNode &node : runtime.graph().nodes()) {
            if (node.node_id == node_id)
                return (output ? node.output_pin_ids : node.input_pin_ids).at(index);
        }
        return -1;
    }

    std::string partNumberOf(int node_id) const {
        for (const GraphNode &node : runtime.graph().nodes()) {
            if (node.node_id == node_id)
                return node.part_number;
        }
        return {};
    }
};

// generator -> splitter (library part) -> attenuator, with the splitter's second output used.
struct SplitterCircuit {
    IComponentEngine *generator = nullptr;
    IComponentEngine *splitter = nullptr;
    IComponentEngine *attenuator = nullptr;
};

SplitterCircuit buildSplitterCircuit(SwapFixture &f, const std::string &part_number) {
    SplitterCircuit circuit;
    circuit.generator = f.add("generator");
    circuit.splitter = f.addPart(part_number);
    circuit.attenuator = f.add("attenuator");
    REQUIRE(circuit.generator != nullptr);
    REQUIRE(circuit.splitter != nullptr);
    REQUIRE(circuit.attenuator != nullptr);
    REQUIRE(circuit.splitter->node().outputs.size() == 2);
    REQUIRE(f.commands.connect(circuit.generator->outputPinId(), circuit.splitter->inputPinId())
                .has_value());
    REQUIRE(f.commands
                .connect(f.pin(circuit.splitter->graphNodeId(), true, 1),
                         circuit.attenuator->inputPinId())
                .has_value());
    return circuit;
}

} // namespace

TEST_CASE("library part swap keeps the component id, its links, and neighbour rewiring",
          "[component_swap][issue200]") {
    SwapFixture f;
    IComponentEngine *generator = f.add("generator");
    IComponentEngine *amp = f.addPart("AM1143");
    IComponentEngine *attenuator = f.add("attenuator");
    REQUIRE(generator != nullptr);
    REQUIRE(amp != nullptr);
    REQUIRE(attenuator != nullptr);
    REQUIRE(f.commands.connect(generator->outputPinId(), amp->inputPinId()).has_value());
    REQUIRE(f.commands.connect(amp->outputPinId(), attenuator->inputPinId()).has_value());
    const ComponentDefinition *zx60 = findPart(f.library, "ZX60-33LN+");
    REQUIRE(zx60 != nullptr);

    const int component_id = amp->id();
    const int old_node = amp->graphNodeId();
    const std::uint64_t revision = f.commands.revision();
    const size_t component_count = f.runtime.components().size();
    const int next_id = f.runtime.nextComponentId();

    const ComponentSwapResult swap = f.commands.swapLibraryPart(f.library, *zx60, old_node);

    REQUIRE(swap.component != nullptr);
    CHECK(swap.error.empty());
    CHECK(swap.component->id() == component_id);
    CHECK(swap.old_graph_node_id == old_node);
    CHECK(swap.new_graph_node_id != old_node);
    CHECK(f.runtime.components().find(old_node) == nullptr);
    CHECK(f.runtime.components().size() == component_count);
    CHECK(f.runtime.nextComponentId() == next_id);
    CHECK(f.commands.revision() == revision + 1);
    CHECK(f.partNumberOf(swap.new_graph_node_id) == "ZX60-33LN+");
    // The replacement starts from its own library parameters, as a fresh placement would.
    CHECK(swap.component->serialize().at("gain_dB").get<double>() == Catch::Approx(28.0));

    // The generator still drives the replacement, which still drives the attenuator.
    REQUIRE(swap.component->node().inputs.size() == 1);
    CHECK(swap.component->node().inputs[0] == &generator->node().outputs[0]);
    CHECK(attenuator->node().inputs[0] == &swap.component->node().outputs[0]);
    CHECK(f.runtime.graph().links().size() == 2);
    CHECK(f.runtime.graph().getSourceForInput(swap.component->inputPinId()).node ==
          &generator->node());
}

TEST_CASE("library part swap refuses a connected port the new part lacks, changing nothing",
          "[component_swap][issue200]") {
    SwapFixture f;
    const SplitterCircuit circuit = buildSplitterCircuit(f, "M1-0002");
    const ComponentDefinition *zx60 = findPart(f.library, "ZX60-33LN+");
    REQUIRE(zx60 != nullptr);

    const int old_node = circuit.splitter->graphNodeId();
    const size_t component_count = f.runtime.components().size();
    const size_t link_count = f.runtime.graph().links().size();
    const std::uint64_t revision = f.commands.revision();
    const int next_id = f.runtime.nextComponentId();

    // The amplifier has one output, but the splitter's second output is linked.
    const ComponentSwapResult swap = f.commands.swapLibraryPart(f.library, *zx60, old_node);

    CHECK(swap.component == nullptr);
    CHECK(swap.error.find("output 2") != std::string::npos);
    CHECK(f.runtime.components().find(old_node) == circuit.splitter);
    CHECK(f.runtime.components().size() == component_count);
    CHECK(f.runtime.graph().links().size() == link_count);
    CHECK(f.commands.revision() == revision);
    CHECK(f.runtime.nextComponentId() == next_id);
    CHECK(circuit.attenuator->node().inputs[0] == &circuit.splitter->node().outputs[1]);
}

TEST_CASE("library part swap to a variant with the same ports keeps every link",
          "[component_swap][issue200]") {
    SwapFixture f;
    const SplitterCircuit circuit = buildSplitterCircuit(f, "M1-0002");
    const ComponentDefinition *mpd = findPart(f.library, "MPD-0226CH");
    REQUIRE(mpd != nullptr);

    const ComponentSwapResult swap =
        f.commands.swapLibraryPart(f.library, *mpd, circuit.splitter->graphNodeId());

    REQUIRE(swap.component != nullptr);
    CHECK(f.partNumberOf(swap.new_graph_node_id) == "MPD-0226CH");
    const SignalSource source =
        f.runtime.graph().getSourceForInput(circuit.attenuator->inputPinId());
    CHECK(source.node == &swap.component->node());
    CHECK(source.output_index == 1);
    CHECK(f.runtime.graph().links().size() == 2);
}

TEST_CASE("library part swap keeps probe slots and group membership on the replacement",
          "[component_swap][issue200]") {
    SwapFixture f;
    IComponentEngine *generator = f.add("generator");
    IComponentEngine *amp = f.addPart("AM1143");
    IComponentEngine *attenuator = f.add("attenuator");
    REQUIRE(generator != nullptr);
    REQUIRE(amp != nullptr);
    REQUIRE(attenuator != nullptr);
    REQUIRE(f.commands.connect(generator->outputPinId(), amp->inputPinId()).has_value());
    REQUIRE(f.commands.connect(amp->outputPinId(), attenuator->inputPinId()).has_value());
    // Slot 0 probes the generator and slot 1 probes the amplifier; the swap must keep that order.
    REQUIRE(f.commands.addProbePin(generator->outputPinId()));
    REQUIRE(f.commands.addProbePin(amp->outputPinId()));
    const int group_id =
        f.commands.createGroup("pair", {generator->graphNodeId(), amp->graphNodeId()});
    REQUIRE(group_id >= 0);
    const ComponentDefinition *zx60 = findPart(f.library, "ZX60-33LN+");
    REQUIRE(zx60 != nullptr);

    const int old_node = amp->graphNodeId();
    const ComponentSwapResult swap = f.commands.swapLibraryPart(f.library, *zx60, old_node);

    REQUIRE(swap.component != nullptr);
    const std::vector<int> &probes = f.runtime.graph().probePins();
    REQUIRE(probes.size() == 2);
    CHECK(probes[0] == generator->outputPinId());
    CHECK(probes[1] == swap.component->outputPinId());

    const Group *group = f.runtime.graph().groupById(group_id);
    REQUIRE(group != nullptr);
    const auto &members = group->member_node_ids;
    CHECK(members.size() == 2);
    CHECK(std::find(members.begin(), members.end(), old_node) == members.end());
    CHECK(std::find(members.begin(), members.end(), swap.new_graph_node_id) != members.end());
}

TEST_CASE("library part swap refuses a probe or instrument on a port the new part lacks",
          "[component_swap][issue200]") {
    SwapFixture f;
    // Only the first output is linked, so the second output is refused on a probe or an instrument
    // alone rather than on its connection.
    IComponentEngine *generator = f.add("generator");
    IComponentEngine *splitter = f.addPart("M1-0002");
    REQUIRE(generator != nullptr);
    REQUIRE(splitter != nullptr);
    REQUIRE(f.commands.connect(generator->outputPinId(), splitter->inputPinId()).has_value());
    const ComponentDefinition *zx60 = findPart(f.library, "ZX60-33LN+");
    REQUIRE(zx60 != nullptr);
    const int old_node = splitter->graphNodeId();
    const int second_output = f.pin(old_node, true, 1);
    const std::uint64_t revision = f.commands.revision();

    SECTION("a probe on the missing port") {
        REQUIRE(f.commands.addProbePin(second_output));
        const std::uint64_t probed_revision = f.commands.revision();
        const ComponentSwapResult swap = f.commands.swapLibraryPart(f.library, *zx60, old_node);
        CHECK(swap.component == nullptr);
        CHECK(swap.error.find("probed") != std::string::npos);
        CHECK(f.runtime.graph().probePins().size() == 1);
        CHECK(f.runtime.components().find(old_node) == splitter);
        CHECK(f.commands.revision() == probed_revision);
    }

    SECTION("an instrument selection on the missing port") {
        const ComponentSwapResult swap =
            f.commands.swapLibraryPart(f.library, *zx60, old_node, {second_output});
        CHECK(swap.component == nullptr);
        CHECK(swap.error.find("instrument") != std::string::npos);
        CHECK(f.runtime.components().find(old_node) == splitter);
        CHECK(f.commands.revision() == revision);
    }
}

TEST_CASE("library part swap moves an instrument selection onto the matching port",
          "[component_swap][issue200]") {
    SwapFixture f;
    const SplitterCircuit circuit = buildSplitterCircuit(f, "M1-0002");
    const ComponentDefinition *mpd = findPart(f.library, "MPD-0226CH");
    REQUIRE(mpd != nullptr);
    const int old_node = circuit.splitter->graphNodeId();
    const int first_output = f.pin(old_node, true, 0);

    const ComponentSwapResult swap =
        f.commands.swapLibraryPart(f.library, *mpd, old_node, {first_output});

    REQUIRE(swap.component != nullptr);
    REQUIRE(swap.pin_map.count(first_output) == 1);
    CHECK(swap.pin_map.at(first_output) == f.pin(swap.new_graph_node_id, true, 0));
}

TEST_CASE("library part swap applies the PFB link policy to the replacement",
          "[component_swap][issue200]") {
    SwapFixture f;
    ComponentDefinition channelizer{};
    channelizer.type = "pfb";
    channelizer.part_number = "TEST-PFB";
    channelizer.parameters = nlohmann::json::object();

    SECTION("an RF generator feeding the channelizer is refused") {
        IComponentEngine *generator = f.add("generator");
        IComponentEngine *amp = f.addPart("AM1143");
        REQUIRE(generator != nullptr);
        REQUIRE(amp != nullptr);
        REQUIRE(f.commands.connect(generator->outputPinId(), amp->inputPinId()).has_value());
        const std::uint64_t revision = f.commands.revision();

        const ComponentSwapResult swap =
            f.commands.swapLibraryPart(f.library, channelizer, amp->graphNodeId());

        CHECK(swap.component == nullptr);
        CHECK(swap.error.find("physical link policy") != std::string::npos);
        CHECK(f.runtime.components().find(amp->graphNodeId()) == amp);
        CHECK(f.commands.revision() == revision);
    }

    SECTION("an ADC feeding the channelizer is allowed") {
        IComponentEngine *adc = f.add("adc");
        IComponentEngine *amp = f.addPart("AM1143");
        IComponentEngine *attenuator = f.add("attenuator");
        REQUIRE(adc != nullptr);
        REQUIRE(amp != nullptr);
        REQUIRE(attenuator != nullptr);
        REQUIRE(f.commands.connect(adc->outputPinId(), amp->inputPinId()).has_value());
        REQUIRE(f.commands.connect(amp->outputPinId(), attenuator->inputPinId()).has_value());

        const ComponentSwapResult swap =
            f.commands.swapLibraryPart(f.library, channelizer, amp->graphNodeId());

        REQUIRE(swap.component != nullptr);
        CHECK(swap.component->type_name() == "pfb");
        const SignalSource source =
            f.runtime.graph().getSourceForInput(swap.component->inputPinId());
        CHECK(source.node == &adc->node());
        CHECK(attenuator->node().inputs[0] == &swap.component->node().outputs[0]);
    }
}

TEST_CASE("library part swap refuses an unbuildable part or a missing component",
          "[component_swap][issue200]") {
    SwapFixture f;
    IComponentEngine *generator = f.add("generator");
    IComponentEngine *amp = f.addPart("AM1143");
    REQUIRE(generator != nullptr);
    REQUIRE(amp != nullptr);
    REQUIRE(f.commands.connect(generator->outputPinId(), amp->inputPinId()).has_value());
    const ComponentDefinition *zx60 = findPart(f.library, "ZX60-33LN+");
    REQUIRE(zx60 != nullptr);
    ComponentDefinition unknown{};
    unknown.type = "not-a-type";
    unknown.part_number = "BOGUS-1";
    unknown.parameters = nlohmann::json::object();

    const std::uint64_t revision = f.commands.revision();
    const size_t component_count = f.runtime.components().size();
    const int next_id = f.runtime.nextComponentId();

    const ComponentSwapResult refused =
        f.commands.swapLibraryPart(f.library, unknown, amp->graphNodeId());
    CHECK(refused.component == nullptr);
    CHECK(refused.error.find("BOGUS-1") != std::string::npos);
    CHECK(f.runtime.components().find(amp->graphNodeId()) == amp);
    CHECK(f.runtime.components().size() == component_count);
    CHECK(f.runtime.nextComponentId() == next_id);
    CHECK(f.commands.revision() == revision);

    const ComponentSwapResult missing = f.commands.swapLibraryPart(f.library, *zx60, 9999);
    CHECK(missing.component == nullptr);
    CHECK(missing.error.find("no longer") != std::string::npos);
    CHECK(f.commands.revision() == revision);
}
