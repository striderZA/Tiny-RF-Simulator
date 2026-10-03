#include "measurement_chain_runner.h"

#include "component_interface.h"
#include "node_graph_engine.h"

#include <algorithm>
#include <functional>
#include <tuple>
#include <unordered_map>

std::string MeasurementChainPath::signature() const {
    std::string result;
    for (size_t i = 0; i < components.size(); ++i) {
        const IComponentEngine *component = components[i];
        if (!component)
            return {};
        result += component->type_name();
        result += ':';
        result += std::to_string(component->id());
        result += component->serialize().dump();
        if (i < output_ports.size())
            result += ':' + std::to_string(output_ports[i]);
        if (i < input_ports.size())
            result += ':' + std::to_string(input_ports[i]);
        result += '|';
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

    int point_b_port = -1;
    for (const auto &node : graph.nodes()) {
        const auto it =
            std::find(node.output_pin_ids.begin(), node.output_pin_ids.end(), point_b_pin);
        if (it != node.output_pin_ids.end()) {
            point_b_port = static_cast<int>(std::distance(node.output_pin_ids.begin(), it));
            break;
        }
    }
    if (point_b_port < 0)
        return std::nullopt;

    using Edge = std::tuple<int, int, int>;
    std::unordered_map<int, int> input_port_by_pin;
    for (const auto &node : graph.nodes())
        for (size_t ii = 0; ii < node.input_pin_ids.size(); ++ii)
            input_port_by_pin.emplace(node.input_pin_ids[ii], static_cast<int>(ii));

    std::unordered_map<int, std::vector<Edge>> next_of;
    for (const auto &node : graph.nodes()) {
        std::vector<Edge> nexts;
        for (size_t oi = 0; oi < node.output_pin_ids.size(); ++oi) {
            const int out_pin = node.output_pin_ids[oi];
            for (const auto &link : graph.links()) {
                if (link.start_pin_id != out_pin)
                    continue;
                const int next_node = graph.nodeIdForPin(link.end_pin_id);
                const auto input = input_port_by_pin.find(link.end_pin_id);
                if (next_node >= 0 && input != input_port_by_pin.end())
                    nexts.emplace_back(static_cast<int>(oi), next_node, input->second);
            }
        }
        std::sort(nexts.begin(), nexts.end());
        nexts.erase(std::unique(nexts.begin(), nexts.end()), nexts.end());
        if (!nexts.empty())
            next_of.emplace(node.node_id, std::move(nexts));
    }

    const auto enters_unsupported_multi_input_node = [&](int node_id) {
        auto *component = host.componentForNode(node_id);
        if (!component || component->numInputPins() <= 1)
            return false;
        if (component->type_name() != "rf_switch_spdt_2to1")
            return true;
        for (const auto &node : graph.nodes()) {
            if (node.node_id != node_id)
                continue;
            const auto connected_inputs =
                std::count_if(graph.links().begin(), graph.links().end(), [&](const auto &link) {
                    return std::find(node.input_pin_ids.begin(), node.input_pin_ids.end(),
                                     link.end_pin_id) != node.input_pin_ids.end();
                });
            return connected_inputs != 1;
        }
        return true;
    };

    constexpr int kMaxDfsSteps = 100000;
    std::vector<int> node_path{start_node};
    std::vector<int> output_path;
    std::vector<int> input_path;
    std::vector<int> found_nodes;
    std::vector<int> found_outputs;
    std::vector<int> found_inputs;
    int path_count = 0;
    int steps = 0;
    std::function<void(int)> dfs = [&](int node) {
        if (path_count >= 2 || ++steps > kMaxDfsSteps)
            return;
        if (node == end_node) {
            ++path_count;
            found_nodes = node_path;
            found_outputs = output_path;
            found_inputs = input_path;
            return;
        }
        const auto it = next_of.find(node);
        if (it == next_of.end())
            return;
        for (const auto &[output_port, next_node, input_port] : it->second) {
            if (path_count >= 2 || steps > kMaxDfsSteps)
                return;
            if (enters_unsupported_multi_input_node(next_node) ||
                std::find(node_path.begin(), node_path.end(), next_node) != node_path.end())
                continue;
            node_path.push_back(next_node);
            output_path.push_back(output_port);
            input_path.push_back(input_port);
            dfs(next_node);
            input_path.pop_back();
            output_path.pop_back();
            node_path.pop_back();
        }
    };
    dfs(start_node);
    if (path_count != 1)
        return std::nullopt;

    MeasurementChainPath result;
    result.components.reserve(found_nodes.size());
    for (int node_id : found_nodes) {
        auto *component = host.componentForNode(node_id);
        if (!component)
            return std::nullopt;
        result.components.push_back(component);
    }
    result.output_ports = std::move(found_outputs);
    result.input_ports = std::move(found_inputs);
    result.point_b_output_port = point_b_port;
    return result;
}

IsolatedChainRunner::IsolatedChainRunner(IMeasurementChainHost &host) : m_host(host) {}

bool IsolatedChainRunner::prepare(const MeasurementChainPath &path) {
    m_last_result = nullptr;
    m_scratch.reset();
    m_clones.clear();
    if (path.components.size() < 2 || path.output_ports.size() + 1 != path.components.size() ||
        path.input_ports.size() + 1 != path.components.size() || path.point_b_output_port < 0)
        return false;

    auto scratch = m_host.beginScratchPass();
    if (!scratch)
        return false;
    std::vector<IComponentEngine *> clones;
    clones.reserve(path.components.size() - 1);
    for (size_t i = 1; i < path.components.size(); ++i) {
        IComponentEngine *live = path.components[i];
        if (!live)
            return false;
        IComponentEngine *clone = scratch->createClone(live->type_name(), live->id());
        if (!clone)
            return false;
        clone->deserialize(live->serialize());
        clones.push_back(clone);
    }

    for (size_t i = 0; i < clones.size(); ++i) {
        auto &inputs = clones[i]->node().inputs;
        const int input_port = path.input_ports[i];
        if (input_port < 0 || static_cast<size_t>(input_port) >= inputs.size())
            return false;
        if (i == 0) {
            m_first_input_port = input_port;
            inputs[static_cast<size_t>(input_port)] = nullptr;
        } else {
            const int output_port = path.output_ports[i];
            auto &outputs = clones[i - 1]->node().outputs;
            if (output_port < 0 || static_cast<size_t>(output_port) >= outputs.size())
                return false;
            inputs[static_cast<size_t>(input_port)] = &outputs[static_cast<size_t>(output_port)];
        }
        if (clones[i]->type_name() == "mixer" && inputs.size() > 1) {
            const auto &live_inputs = path.components[i + 1]->node().inputs;
            if (live_inputs.size() > 1)
                inputs[1] = live_inputs[1];
        }
    }
    auto &outputs = clones.back()->node().outputs;
    if (static_cast<size_t>(path.point_b_output_port) >= outputs.size())
        return false;

    m_scratch = std::move(scratch);
    m_clones = std::move(clones);
    m_point_b_output_port = path.point_b_output_port;
    return true;
}

const Spectrum *IsolatedChainRunner::run(const Spectrum &stimulus) {
    m_last_result = nullptr;
    if (!m_scratch || m_clones.empty())
        return nullptr;
    auto &first_inputs = m_clones.front()->node().inputs;
    if (m_first_input_port < 0 || static_cast<size_t>(m_first_input_port) >= first_inputs.size())
        return nullptr;
    first_inputs[static_cast<size_t>(m_first_input_port)] = &stimulus;
    for (auto *clone : m_clones)
        clone->update(0.0);
    auto &outputs = m_clones.back()->node().outputs;
    if (static_cast<size_t>(m_point_b_output_port) >= outputs.size())
        return nullptr;
    m_last_result = &outputs[static_cast<size_t>(m_point_b_output_port)];
    return m_last_result;
}
