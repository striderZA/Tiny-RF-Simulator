#include "rewire.h"

#include "graph_link_policy.h"
#include "signal_node.h"

void rewireComponentInputs(std::span<IComponentEngine *const> components,
                           const NodeGraphEngine &graph) {
    for (auto *component : components) {
        if (!component)
            continue;

        const int n_inputs = component->numInputPins();
        for (int k = 0; k < n_inputs; ++k) {
            if (static_cast<size_t>(k) >= component->node().inputs.size())
                continue;

            const int pin = component->inputPinId(k);
            SignalSource source;
            if (pin >= 0)
                source = graph.getSourceForInput(pin);

            IComponentEngine *source_component = nullptr;
            if (source.node) {
                for (auto *candidate : components) {
                    if (candidate && &candidate->node() == source.node) {
                        source_component = candidate;
                        break;
                    }
                }
            }

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
