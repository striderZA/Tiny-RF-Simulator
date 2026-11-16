#include "circuit_runtime.h"
#include "graph_link_policy.h"
#include "rewire.h"
#include <algorithm>
#include <iterator>
#include <vector>

namespace {
struct ResolvedPin {
    const GraphNode *graph_node = nullptr;
    IComponentEngine *component = nullptr;
    size_t port_index = 0;
};

ResolvedPin resolvePin(const NodeGraphEngine &graph, const ComponentRegistry &components,
                       int pin_id, bool output) {
    const int node_id = graph.nodeIdForPin(pin_id);
    if (node_id < 0)
        return {};

    const auto node_it =
        std::find_if(graph.nodes().begin(), graph.nodes().end(),
                     [node_id](const GraphNode &node) { return node.node_id == node_id; });
    if (node_it == graph.nodes().end())
        return {};

    IComponentEngine *component = components.find(node_id);
    if (!component || component->graphNodeId() != node_id ||
        node_it->signal_node != &component->node())
        return {};

    const auto &pin_ids = output ? node_it->output_pin_ids : node_it->input_pin_ids;
    const auto pin_it = std::find(pin_ids.begin(), pin_ids.end(), pin_id);
    if (pin_it == pin_ids.end())
        return {};

    const size_t port_index = static_cast<size_t>(std::distance(pin_ids.begin(), pin_it));
    if (output) {
        if (port_index >= component->node().outputs.size() ||
            component->outputPinId(static_cast<int>(port_index)) != pin_id)
            return {};
    } else if (port_index >= component->node().inputs.size() ||
               component->inputPinId(static_cast<int>(port_index)) != pin_id) {
        return {};
    }

    return {&*node_it, component, port_index};
}
} // namespace

CircuitRuntime::CircuitRuntime() : m_components(m_graph, m_view_manager) {}

IComponentEngine *CircuitRuntime::createComponent(const ComponentFactory &factory) {
    const int reserved_id = m_next_component_id++;
    return factory(m_components, m_graph, reserved_id);
}

IComponentEngine *CircuitRuntime::createComponentWithId(const ComponentFactory &factory,
                                                        int component_id) {
    return factory(m_components, m_graph, component_id);
}

IComponentEngine *CircuitRuntime::restoreComponent(const ComponentFactory &factory,
                                                   int component_id, int graph_node_id,
                                                   const std::vector<int> &input_pins,
                                                   const std::vector<int> &output_pins) {
    if (m_components.find(graph_node_id))
        return nullptr;
    for (IComponentEngine *component : m_components.all()) {
        if (component && component->id() == component_id)
            return nullptr;
    }
    for (const GraphNode &node : m_graph.nodes()) {
        if (node.node_id == graph_node_id)
            return nullptr;
    }

    const int prior_next_node_id = m_graph.nextNodeId();
    const int prior_next_pin_id = m_graph.nextPinId();
    const int prior_next_link_id = m_graph.nextLinkId();
    const int prior_next_component_id = m_next_component_id;

    // addNode() allocates inputs before outputs, so the first input pin (or the
    // first output pin of a source) seeds the pin counter.
    const int first_pin_id = !input_pins.empty()    ? input_pins.front()
                             : !output_pins.empty() ? output_pins.front()
                                                    : prior_next_pin_id;
    m_graph.setNextIds(graph_node_id, first_pin_id, prior_next_link_id);

    const auto restore_counters = [&]() {
        m_graph.setNextIds(std::max(prior_next_node_id, m_graph.nextNodeId()),
                           std::max(prior_next_pin_id, m_graph.nextPinId()),
                           std::max(prior_next_link_id, m_graph.nextLinkId()));
        m_next_component_id = std::max(prior_next_component_id, component_id + 1);
    };

    IComponentEngine *component = nullptr;
    try {
        component = factory(m_components, m_graph, component_id);
    } catch (...) {
        // The counters were lowered before the call; restore them before the
        // exception leaves, so live ids are never reissued.
        restore_counters();
        throw;
    }
    if (!component) {
        restore_counters();
        return nullptr;
    }

    const auto &nodes = m_graph.nodes();
    const auto built = std::find_if(nodes.begin(), nodes.end(), [&](const GraphNode &node) {
        return node.node_id == component->graphNodeId();
    });
    const bool rebuilt_as_requested = built != nodes.end() && built->node_id == graph_node_id &&
                                      built->input_pin_ids == input_pins &&
                                      built->output_pin_ids == output_pins;
    if (!rebuilt_as_requested) {
        m_components.remove(component->graphNodeId());
        restore_counters();
        return nullptr;
    }

    restore_counters();
    rewireComponentInputs(m_components.all(), m_graph);
    return component;
}

bool CircuitRuntime::removeComponent(int graph_node_id) {
    if (!m_components.remove(graph_node_id))
        return false;

    rewireComponentInputs(m_components.all(), m_graph);
    return true;
}

std::optional<int> CircuitRuntime::connect(int start_pin_id, int end_pin_id) {
    const ResolvedPin source = resolvePin(m_graph, m_components, start_pin_id, true);
    const ResolvedPin target = resolvePin(m_graph, m_components, end_pin_id, false);
    if (!source.graph_node || !target.graph_node)
        return std::nullopt;

    if (!graphLinkAllowed(source.component, target.component, start_pin_id, end_pin_id) ||
        !m_graph.canAddLink(start_pin_id, end_pin_id))
        return std::nullopt;

    const int link_id = m_graph.addLink(start_pin_id, end_pin_id);
    rewireComponentInputs(m_components.all(), m_graph);
    return link_id;
}

bool CircuitRuntime::disconnect(int link_id) {
    const auto &links = m_graph.links();
    const auto link_it = std::find_if(links.begin(), links.end(), [link_id](const GraphLink &link) {
        return link.link_id == link_id;
    });
    if (link_it == links.end())
        return false;

    m_graph.removeLink(link_id);
    rewireComponentInputs(m_components.all(), m_graph);
    return true;
}

void CircuitRuntime::update(double dt) {
    rewireComponentInputs(m_components.all(), m_graph);
    for (int node_id : m_graph.topologicalOrder()) {
        IComponentEngine *component = m_components.find(node_id);
        if (component)
            component->update(dt);
    }
}

void CircuitRuntime::clearComponentsAndResetIds() {
    m_graph.removeAllLinks();

    std::vector<int> component_node_ids;
    component_node_ids.reserve(m_components.size());
    for (IComponentEngine *component : m_components.all()) {
        if (component)
            component_node_ids.push_back(component->graphNodeId());
    }
    for (int node_id : component_node_ids)
        m_components.remove(node_id);

    rewireComponentInputs(m_components.all(), m_graph);
    m_graph.setNextIds(1, 100, 1000);
    m_next_component_id = 100;
    ++m_epoch;
}

const NodeGraphEngine &CircuitRuntime::graph() const { return m_graph; }

const ComponentRegistry &CircuitRuntime::components() const { return m_components; }

const ViewManager &CircuitRuntime::viewManager() const { return m_view_manager; }

int CircuitRuntime::nextComponentId() const { return m_next_component_id; }

void CircuitRuntime::setNextComponentId(int id) { m_next_component_id = id; }

std::uint64_t CircuitRuntime::epoch() const { return m_epoch; }
