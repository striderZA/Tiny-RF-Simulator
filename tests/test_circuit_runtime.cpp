#include "adc_engine.h"
#include "amplifier_engine.h"
#include "circuit_runtime.h"
#include "pfb_channelizer_engine.h"
#include "signal_generator_engine.h"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {
template <typename Engine>
IComponentEngine *createEngine(ComponentRegistry &components, NodeGraphEngine &graph, int id) {
    return &components.add<Engine>(id, graph);
}
class TwoInputTestEngine : public ComponentEngineBase {
  public:
    TwoInputTestEngine(int id, NodeGraphEngine &graph)
        : ComponentEngineBase(id, graph, "Two-input test", 2, 1) {}

    int inputPinId(int port) const override {
        if (port < 0 || port >= 2)
            return -1;
        for (const auto &node : m_graph->nodes()) {
            if (node.node_id == m_graph_node_id)
                return node.input_pin_ids[static_cast<size_t>(port)];
        }
        return -1;
    }
    int numInputPins() const override { return 2; }
    std::string_view type_name() const override { return "two_input_test"; }
    std::string hoverSummary() const override { return {}; }
    void update(double) override {}
};

const GraphNode &graphNodeFor(const CircuitRuntime &runtime, const IComponentEngine &engine) {
    const auto &nodes = runtime.graph().nodes();
    const auto it = std::find_if(nodes.begin(), nodes.end(), [&](const GraphNode &node) {
        return node.node_id == engine.graphNodeId();
    });
    REQUIRE(it != nodes.end());
    return *it;
}
} // namespace

TEST_CASE("CircuitRuntime wires and updates a generator-amplifier chain", "[circuit_runtime]") {
    CircuitRuntime runtime;
    auto *generator = static_cast<SignalGeneratorEngine *>(
        runtime.createComponent(createEngine<SignalGeneratorEngine>));
    auto *amplifier =
        static_cast<AmplifierEngine *>(runtime.createComponent(createEngine<AmplifierEngine>));
    REQUIRE(generator != nullptr);
    REQUIRE(amplifier != nullptr);
    generator->addTone(100e6, -20.0);
    amplifier->setGain_dB(12.0);

    const auto link = runtime.connect(generator->outputPinId(), amplifier->inputPinId());

    REQUIRE(link.has_value());
    REQUIRE(runtime.graph().links().size() == 1);
    REQUIRE(amplifier->node().inputs[0] == &generator->node().outputs[0]);
    runtime.update(0.0);

    const auto &tones = amplifier->node().outputs[0].tones;
    REQUIRE(tones.size() == 1);
    CHECK(tones[0].freq_Hz == Catch::Approx(100e6));
    CHECK(tones[0].power_dBm == Catch::Approx(-8.0));
}

TEST_CASE("CircuitRuntime rejects a second link into an occupied input", "[circuit_runtime]") {
    CircuitRuntime runtime;
    auto *first = runtime.createComponent(createEngine<SignalGeneratorEngine>);
    auto *second = runtime.createComponent(createEngine<AmplifierEngine>);
    auto *third = runtime.createComponent(createEngine<AmplifierEngine>);
    REQUIRE(runtime.connect(first->outputPinId(), second->inputPinId()).has_value());
    const size_t links_before_duplicate = runtime.graph().links().size();

    CHECK_FALSE(runtime.connect(third->outputPinId(), second->inputPinId()).has_value());
    CHECK(runtime.graph().links().size() == links_before_duplicate);
}

TEST_CASE("CircuitRuntime rejects a cycle-closing link without changing the graph",
          "[circuit_runtime]") {
    CircuitRuntime runtime;
    auto *first = runtime.createComponent(createEngine<TwoInputTestEngine>);
    auto *second = runtime.createComponent(createEngine<TwoInputTestEngine>);
    REQUIRE(runtime.connect(first->outputPinId(), second->inputPinId(1)).has_value());
    const size_t links_before_cycle = runtime.graph().links().size();

    CHECK_FALSE(runtime.connect(second->outputPinId(), first->inputPinId(0)).has_value());
    CHECK(runtime.graph().links().size() == links_before_cycle);
}

TEST_CASE("CircuitRuntime rejects unresolved and wrong-direction pins before linking",
          "[circuit_runtime]") {
    CircuitRuntime runtime;
    auto *source = runtime.createComponent(createEngine<AmplifierEngine>);
    auto *target = runtime.createComponent(createEngine<AmplifierEngine>);
    const auto &source_node = graphNodeFor(runtime, *source);
    const auto &target_node = graphNodeFor(runtime, *target);
    const size_t initial_links = runtime.graph().links().size();

    CHECK_FALSE(runtime.connect(-1, target->inputPinId()).has_value());
    CHECK_FALSE(runtime.connect(source->inputPinId(), target->outputPinId()).has_value());
    CHECK_FALSE(runtime.connect(source->outputPinId(), target->outputPinId()).has_value());
    CHECK_FALSE(runtime.connect(source->inputPinId(), target->inputPinId()).has_value());
    CHECK(runtime.graph().links().size() == initial_links);
    CHECK(source_node.signal_node == &source->node());
    CHECK(target_node.signal_node == &target->node());
}

TEST_CASE("CircuitRuntime applies the ADC-only PFB connection policy", "[circuit_runtime]") {
    CircuitRuntime runtime;
    auto *generator = runtime.createComponent(createEngine<SignalGeneratorEngine>);
    auto *adc = runtime.createComponent(createEngine<AdcEngine>);
    auto *pfb = runtime.createComponent(createEngine<PFBChannelizerEngine>);

    CHECK_FALSE(runtime.connect(generator->outputPinId(), pfb->inputPinId()).has_value());
    CHECK(runtime.graph().links().empty());
    const auto gen_to_adc = runtime.connect(generator->outputPinId(), adc->inputPinId());
    REQUIRE(gen_to_adc.has_value());
    const auto adc_to_pfb = runtime.connect(adc->outputPinId(), pfb->inputPinId());
    REQUIRE(adc_to_pfb.has_value());
    CHECK(runtime.graph().links().size() == 2);
}

TEST_CASE("CircuitRuntime synchronously clears rewired input on disconnect", "[circuit_runtime]") {
    CircuitRuntime runtime;
    auto *generator = runtime.createComponent(createEngine<SignalGeneratorEngine>);
    auto *amplifier = runtime.createComponent(createEngine<AmplifierEngine>);
    const auto link = runtime.connect(generator->outputPinId(), amplifier->inputPinId());
    REQUIRE(link.has_value());
    REQUIRE(amplifier->node().inputs[0] == &generator->node().outputs[0]);

    CHECK(runtime.disconnect(*link));
    CHECK(amplifier->node().inputs[0] == nullptr);
    CHECK(runtime.graph().links().empty());
    CHECK_FALSE(runtime.disconnect(*link));
}

TEST_CASE("CircuitRuntime clears downstream input before returning from upstream removal",
          "[circuit_runtime]") {
    CircuitRuntime runtime;
    auto *adc = runtime.createComponent(createEngine<AdcEngine>);
    auto *pfb = runtime.createComponent(createEngine<PFBChannelizerEngine>);
    REQUIRE(runtime.connect(adc->outputPinId(), pfb->inputPinId()).has_value());
    REQUIRE(pfb->node().inputs[0] == &adc->node().outputs[0]);

    CHECK(runtime.removeComponent(adc->graphNodeId()));
    CHECK(pfb->node().inputs[0] == nullptr);
    CHECK(runtime.graph().links().empty());
}

TEST_CASE("CircuitRuntime consumes reserved component IDs and resets IDs on clear",
          "[circuit_runtime]") {
    CircuitRuntime runtime;
    CHECK(runtime.nextComponentId() == 100);
    CHECK_THROWS(runtime.createComponent(
        [](ComponentRegistry &, NodeGraphEngine &, int) -> IComponentEngine * {
            throw std::runtime_error("factory failed");
        }));
    CHECK(runtime.nextComponentId() == 101);

    auto *generator = runtime.createComponent(createEngine<SignalGeneratorEngine>);
    CHECK(generator->id() == 101);
    REQUIRE(runtime
                .connect(generator->outputPinId(),
                         runtime.createComponent(createEngine<AmplifierEngine>)->inputPinId())
                .has_value());
    runtime.clearComponentsAndResetIds();
    CHECK(runtime.graph().nodes().empty());
    CHECK(runtime.graph().links().empty());
    CHECK(runtime.components().size() == 0);
    CHECK(runtime.nextComponentId() == 100);
    CHECK((std::is_const_v<std::remove_reference_t<decltype(runtime.graph())>>));
}
