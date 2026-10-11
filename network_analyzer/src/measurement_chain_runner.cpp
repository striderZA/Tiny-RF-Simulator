#include "measurement_chain_runner.h"

#include "component_interface.h"
#include "node_graph_engine.h"

#include <algorithm>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

std::string MeasurementChainPath::signature() const {
    std::string result;
    for (const IComponentEngine *component : components) {
        if (!component)
            return {};
        result += component->type_name();
        result += ':';
        result += std::to_string(component->id());
        result += component->serialize().dump();
        result += '|';
    }
    for (const auto &edge : edges) {
        result += std::to_string(edge.from) + '.' + std::to_string(edge.output_port) + '>' +
                  std::to_string(edge.to) + '.' + std::to_string(edge.input_port) + ';';
    }
    result += "B:" + std::to_string(point_b_output_port);
    return result;
}

std::optional<MeasurementChainPath> findMeasurementChainPath(const NodeGraphEngine &graph,
                                                             const IMeasurementChainHost &host,
                                                             int point_a_pin, int point_b_pin) {
    const int start_node = graph.nodeIdForPin(point_a_pin);
    const int end_node = graph.nodeIdForPin(point_b_pin);
    if (start_node < 0 || end_node < 0 || start_node == end_node)
        return std::nullopt;

    // Pin -> (node, port) for both pin directions. Keeping the port lets the
    // clones reproduce the exact wiring through multi-port components.
    struct PinOwner {
        int node;
        int port;
    };
    std::unordered_map<int, PinOwner> input_owner;
    std::unordered_map<int, PinOwner> output_owner;
    for (const auto &node : graph.nodes()) {
        for (size_t i = 0; i < node.input_pin_ids.size(); ++i)
            input_owner.emplace(node.input_pin_ids[i], PinOwner{node.node_id, static_cast<int>(i)});
        for (size_t o = 0; o < node.output_pin_ids.size(); ++o)
            output_owner.emplace(node.output_pin_ids[o],
                                 PinOwner{node.node_id, static_cast<int>(o)});
    }
    // Point B reads one exact output port of its component.
    const auto point_b = output_owner.find(point_b_pin);
    if (point_b == output_owner.end())
        return std::nullopt;

    // Distinct links between known pins. Duplicate links (same start pin ->
    // same end pin, which the graph allows) collapse so they cannot fake a
    // second feed into one input.
    struct Link {
        PinOwner from;
        PinOwner to;
    };
    std::set<std::pair<int, int>> seen_pins;
    std::vector<Link> links;
    for (const auto &link : graph.links()) {
        const auto from = output_owner.find(link.start_pin_id);
        const auto to = input_owner.find(link.end_pin_id);
        if (from == output_owner.end() || to == input_owner.end())
            continue;
        if (!seen_pins.emplace(link.start_pin_id, link.end_pin_id).second)
            continue;
        links.push_back({from->second, to->second});
    }

    // Adjacency built once keeps the walks and the sort below
    // O((V + E) log V) instead of rescanning every link per node.
    std::unordered_map<int, std::vector<const Link *>> out_links;
    std::unordered_map<int, std::vector<const Link *>> in_links;
    for (const auto &link : links) {
        out_links[link.from.node].push_back(&link);
        in_links[link.to.node].push_back(&link);
    }

    // Forward reach from Point A's component. The stimulus replaces its
    // output, so the walk never re-enters it and whatever feeds it is
    // irrelevant.
    std::unordered_set<int> forward;
    std::vector<int> stack{start_node};
    while (!stack.empty()) {
        const int node = stack.back();
        stack.pop_back();
        for (const Link *link : out_links[node]) {
            if (link->to.node != start_node && forward.insert(link->to.node).second)
                stack.push_back(link->to.node);
        }
    }
    if (!forward.count(end_node))
        return std::nullopt;

    // Keep only forward-reached components that can still reach Point B;
    // dead-end branches (e.g. a splitter output going elsewhere) do not
    // affect it.
    std::unordered_set<int> members{end_node};
    stack.assign(1, end_node);
    while (!stack.empty()) {
        const int node = stack.back();
        stack.pop_back();
        for (const Link *link : in_links[node]) {
            if (forward.count(link->from.node) && members.insert(link->from.node).second)
                stack.push_back(link->from.node);
        }
    }

    // Every member input must be reproducible on the clones: fed by Point A's
    // component (the stimulus) or by another member. A feed from neither is a
    // live source the clones cannot represent, so reject it rather than report
    // a plausible but wrong NF. The stimulus replaces every output of Point
    // A's component alike, so feeds through two of its outputs would ignore
    // how that component divides its signal (e.g. a 1:2 switch's throw).
    std::optional<int> point_a_output;
    std::unordered_map<int, IComponentEngine *> component_of;
    for (int node_id : members) {
        std::unordered_set<int> fed_ports;
        for (const Link *link : in_links[node_id]) {
            if (link->from.node == start_node) {
                if (point_a_output && *point_a_output != link->from.port)
                    return std::nullopt;
                point_a_output = link->from.port;
            } else if (!members.count(link->from.node)) {
                return std::nullopt;
            }
            if (!fed_ports.insert(link->to.port).second)
                return std::nullopt; // two links into one input pin
        }
        auto *component = host.componentForNode(node_id);
        if (!component)
            return std::nullopt;
        // Only the 2:1 switch defines how two inputs combine (selected throw
        // at insertion loss, the other at isolation). Any other multi-input
        // component, such as a combiner, stays unsupported even when singly
        // fed.
        if (component->numInputPins() > 1 && component->type_name() != "rf_switch_spdt_2to1")
            return std::nullopt;
        component_of.emplace(node_id, component);
    }
    auto *start_component = host.componentForNode(start_node);
    if (!start_component)
        return std::nullopt;

    // Kahn topological sort; ties break by graph node order so the result is
    // deterministic. A leftover member means a cycle, which one feed-forward
    // clone pass cannot evaluate. Feeds from Point A's component are already
    // available and do not count as pending inputs.
    std::unordered_map<int, size_t> graph_order;
    for (const auto &node : graph.nodes())
        graph_order.emplace(node.node_id, graph_order.size());
    std::unordered_map<int, int> pending_inputs;
    std::set<std::pair<size_t, int>> ready;
    for (int node_id : members) {
        int pending = 0;
        for (const Link *link : in_links[node_id])
            pending += link->from.node != start_node ? 1 : 0;
        pending_inputs[node_id] = pending;
        if (pending == 0)
            ready.emplace(graph_order.at(node_id), node_id);
    }

    MeasurementChainPath result;
    result.components.push_back(start_component);
    std::unordered_map<int, size_t> index_of{{start_node, 0}};
    while (!ready.empty()) {
        const int node_id = ready.begin()->second;
        ready.erase(ready.begin());
        index_of.emplace(node_id, result.components.size());
        result.components.push_back(component_of.at(node_id));
        for (const Link *link : out_links[node_id]) {
            if (members.count(link->to.node) && --pending_inputs[link->to.node] == 0)
                ready.emplace(graph_order.at(link->to.node), link->to.node);
        }
    }
    if (index_of.size() != members.size() + 1)
        return std::nullopt; // cycle

    // Every other member is an ancestor of Point B's component, so it is the
    // only sink and Kahn's order places it last. Check rather than assume:
    // the runner reads Point B's response from the last clone.
    if (index_of.at(end_node) != result.components.size() - 1)
        return std::nullopt;

    for (int node_id : members) {
        for (const Link *link : in_links[node_id])
            result.edges.push_back({index_of.at(link->from.node), link->from.port,
                                    index_of.at(node_id), link->to.port});
    }
    std::sort(result.edges.begin(), result.edges.end(), [](const auto &a, const auto &b) {
        return std::tie(a.to, a.input_port, a.from, a.output_port) <
               std::tie(b.to, b.input_port, b.from, b.output_port);
    });
    result.point_b_output_port = point_b->second.port;
    return result;
}

IsolatedChainRunner::IsolatedChainRunner(const IMeasurementChainHost &host) : m_host(host) {}

bool IsolatedChainRunner::prepare(const MeasurementChainPath &path) {
    m_last_result = nullptr;
    m_scratch.reset();
    m_clones.clear();
    m_stimulus_inputs.clear();
    const size_t count = path.components.size();
    if (count < 2 || !path.components[0] || path.point_b_output_port < 0)
        return false;

    auto scratch = m_host.beginScratchPass();
    if (!scratch)
        return false;
    std::vector<IComponentEngine *> clones(count, nullptr);
    for (size_t i = 1; i < count; ++i) {
        IComponentEngine *live = path.components[i];
        if (!live)
            return false;
        IComponentEngine *clone = scratch->createClone(live->type_name(), live->id());
        if (!clone)
            return false;
        clone->deserialize(live->serialize());
        clones[i] = clone;
    }

    // Wire by assigning SignalNode input pointers directly; no scratch-graph
    // links are needed. An input without an edge stays null, exactly as an
    // unlinked live input does. Every edge must point forward, so each
    // clone's inputs are final before it runs, and feed an input no other
    // edge feeds.
    std::vector<std::pair<size_t, size_t>> stimulus_inputs;
    std::set<std::pair<size_t, size_t>> wired_inputs;
    for (const auto &edge : path.edges) {
        if (edge.to == 0 || edge.to >= count || edge.from >= edge.to || edge.input_port < 0)
            return false;
        auto &inputs = clones[edge.to]->node().inputs;
        const auto input_port = static_cast<size_t>(edge.input_port);
        if (input_port >= inputs.size() || !wired_inputs.emplace(edge.to, input_port).second)
            return false;
        // Point A's component is never cloned: its edges carry the stimulus,
        // but their source port must still be one of its real outputs.
        const auto &outputs =
            edge.from == 0 ? path.components[0]->node().outputs : clones[edge.from]->node().outputs;
        if (edge.output_port < 0 || static_cast<size_t>(edge.output_port) >= outputs.size())
            return false;
        if (edge.from == 0)
            stimulus_inputs.emplace_back(edge.to, input_port);
        else
            inputs[input_port] = &outputs[static_cast<size_t>(edge.output_port)];
    }
    // Without a stimulus feed the clones would measure silence, not the path.
    if (stimulus_inputs.empty() ||
        static_cast<size_t>(path.point_b_output_port) >= clones.back()->node().outputs.size())
        return false;

    m_scratch = std::move(scratch);
    m_clones = std::move(clones);
    m_stimulus_inputs = std::move(stimulus_inputs);
    m_point_b_output_port = path.point_b_output_port;
    return true;
}

const Spectrum *IsolatedChainRunner::run(const Spectrum &stimulus) {
    m_last_result = nullptr;
    if (!m_scratch || m_clones.size() < 2)
        return nullptr;
    for (const auto &[clone, input_port] : m_stimulus_inputs)
        m_clones[clone]->node().inputs[input_port] = &stimulus;
    // components[] is topologically ordered, so every clone's inputs are final
    // before it runs.
    for (size_t i = 1; i < m_clones.size(); ++i)
        m_clones[i]->update(0.0);
    auto &outputs = m_clones.back()->node().outputs;
    if (static_cast<size_t>(m_point_b_output_port) >= outputs.size())
        return nullptr;
    m_last_result = &outputs[static_cast<size_t>(m_point_b_output_port)];
    return m_last_result;
}
