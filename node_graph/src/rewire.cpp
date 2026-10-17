#include "rewire.h"

#include "graph_link_policy.h"
#include "signal_node.h"
#include <unordered_map>

void rewireComponentInputs(std::span<IComponentEngine *const> components,
                           const NodeGraphEngine &graph) {
    std::unordered_map<int, SignalSource> source_by_output_pin;
    source_by_output_pin.reserve(graph.links().size());
    for (const auto &node : graph.nodes()) {
        for (size_t output_index = 0; output_index < node.output_pin_ids.size(); ++output_index) {
            source_by_output_pin.try_emplace(
                node.output_pin_ids[output_index],
                SignalSource{node.signal_node, static_cast<int>(output_index)});
        }
    }

    std::unordered_map<int, SignalSource> source_by_input_pin;
    source_by_input_pin.reserve(graph.links().size());
    for (const auto &link : graph.links()) {
        const auto source_it = source_by_output_pin.find(link.start_pin_id);
        if (source_it != source_by_output_pin.end())
            source_by_input_pin.try_emplace(link.end_pin_id, source_it->second);
    }

    std::unordered_map<const SignalNode *, IComponentEngine *> source_components;
    source_components.reserve(components.size());
    for (auto *component : components) {
        if (component)
            source_components.try_emplace(&component->node(), component);
    }

    for (auto *component : components) {
        if (!component)
            continue;

        const int n_inputs = component->numInputPins();
        for (int k = 0; k < n_inputs; ++k) {
            if (static_cast<size_t>(k) >= component->node().inputs.size())
                continue;

            const int pin = component->inputPinId(k);
            SignalSource source;
            if (pin >= 0) {
                const auto source_it = source_by_input_pin.find(pin);
                if (source_it != source_by_input_pin.end())
                    source = source_it->second;
            }

            IComponentEngine *source_component = nullptr;
            const auto source_component_it = source_components.find(source.node);
            if (source_component_it != source_components.end())
                source_component = source_component_it->second;

            const int source_pin = (source_component && source.output_index >= 0)
                                       ? source_component->outputPinId(source.output_index)
                                       : -1;
            const bool allowed = graphLinkAllowed(source_component, component, source_pin, pin);
            const bool usable =
                allowed && source.node && source.output_index >= 0 &&
                static_cast<size_t>(source.output_index) < source.node->outputs.size();
            component->node().inputs[static_cast<size_t>(k)] =
                usable ? &source.node->outputs[static_cast<size_t>(source.output_index)] : nullptr;
        }
    }
}
