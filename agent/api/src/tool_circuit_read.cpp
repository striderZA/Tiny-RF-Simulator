#include "agent_tools.h"

#include "agent_args.h"
#include "component_type_registry.h"
#include "node_graph_engine.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Json = nlohmann::json;

const GraphNode *graphNode(const NodeGraphEngine &graph, int node_id) {
    const auto &nodes = graph.nodes();
    const auto found = std::find_if(nodes.begin(), nodes.end(), [node_id](const GraphNode &node) {
        return node.node_id == node_id;
    });
    return found == nodes.end() ? nullptr : &*found;
}

IComponentEngine *componentForSignal(const CircuitRuntime &runtime, const SignalNode *signal) {
    if (!signal)
        return nullptr;
    for (auto *component : runtime.components().all()) {
        if (&component->node() == signal)
            return component;
    }
    return nullptr;
}

int portIndex(const std::vector<int> &pins, int pin_id) {
    const auto found = std::find(pins.begin(), pins.end(), pin_id);
    return found == pins.end() ? -1 : static_cast<int>(found - pins.begin());
}

std::string portLabel(const std::vector<std::string> &labels, int port, std::string_view prefix) {
    if (port >= 0 && static_cast<std::size_t>(port) < labels.size() &&
        !labels[static_cast<std::size_t>(port)].empty())
        return labels[static_cast<std::size_t>(port)];
    std::string result(prefix);
    if (port > 0)
        result += std::to_string(port + 1);
    return result;
}

Json endpointJson(const IComponentEngine &component, int port) {
    return Json{{"component", component.id()}, {"port", port}};
}

Json outputEndpoint(const AgentApiContext &context, int pin_id) {
    const auto &runtime = context.runtime;
    const auto &graph = runtime.graph();
    const int node_id = graph.nodeIdForPin(pin_id);
    const GraphNode *node = graphNode(graph, node_id);
    IComponentEngine *component = runtime.components().find(node_id);
    if (!node || !component)
        return nullptr;
    const int output_port = portIndex(node->output_pin_ids, pin_id);
    if (output_port < 0)
        return nullptr;
    return endpointJson(*component, output_port);
}

Json componentPorts(const AgentApiContext &context, const GraphNode &node) {
    const auto &graph = context.runtime.graph();
    Json inputs = Json::array();
    for (std::size_t port = 0; port < node.input_pin_ids.size(); ++port) {
        const int pin_id = node.input_pin_ids[port];
        const SignalSource source = graph.getSourceForInput(pin_id);
        IComponentEngine *source_component = componentForSignal(context.runtime, source.node);
        Json source_json = nullptr;
        if (source_component && source.output_index >= 0)
            source_json = endpointJson(*source_component, source.output_index);
        inputs.push_back({{"port", port},
                          {"label", portLabel(node.input_labels, static_cast<int>(port), "IN")},
                          {"source", std::move(source_json)}});
    }

    Json outputs = Json::array();
    for (std::size_t port = 0; port < node.output_pin_ids.size(); ++port) {
        const int pin_id = node.output_pin_ids[port];
        Json destinations = Json::array();
        for (const auto &link : graph.links()) {
            if (link.start_pin_id != pin_id)
                continue;
            const int target_node_id = graph.nodeIdForPin(link.end_pin_id);
            IComponentEngine *target = context.runtime.components().find(target_node_id);
            const GraphNode *target_node = graphNode(graph, target_node_id);
            if (!target || !target_node)
                continue;
            const int input_port = portIndex(target_node->input_pin_ids, link.end_pin_id);
            if (input_port >= 0)
                destinations.push_back(endpointJson(*target, input_port));
        }
        outputs.push_back({{"port", port},
                           {"label", portLabel(node.output_labels, static_cast<int>(port), "OUT")},
                           {"destinations", std::move(destinations)}});
    }
    return Json{{"inputs", std::move(inputs)}, {"outputs", std::move(outputs)}};
}

Json probeEndpoints(const AgentApiContext &context) {
    const auto &runtime = context.runtime;
    const auto &graph = runtime.graph();
    const auto &probe_pins = graph.probePins();
    const auto probed_sources = graph.probedSignalNodes();
    Json probes = Json::array();
    for (std::size_t index = 0; index < probe_pins.size(); ++index) {
        const auto &source = probed_sources[index];
        IComponentEngine *component = componentForSignal(runtime, source.node);
        if (component && source.output_index >= 0) {
            probes.push_back(endpointJson(*component, source.output_index));
            continue;
        }
        const int node_id = graph.nodeIdForPin(probe_pins[index]);
        const auto *node = graphNode(graph, node_id);
        component = runtime.components().find(node_id);
        if (!component || !node)
            continue;
        int port = portIndex(node->output_pin_ids, probe_pins[index]);
        if (port < 0)
            port = portIndex(node->input_pin_ids, probe_pins[index]);
        if (port >= 0)
            probes.push_back(endpointJson(*component, port));
    }
    return probes;
}

Json parameterInfo(const IComponentEngine &component) {
    Json result = Json::array();
    const auto *descriptor = ComponentTypeRegistry::instance().find(component.type_name());
    if (!descriptor)
        return result;
    for (const auto &field : descriptor->state_fields) {
        std::string kind;
        switch (field.kind) {
        case FieldKind::Number:
            kind = "number";
            break;
        case FieldKind::String:
            kind = "string";
            break;
        case FieldKind::Enum:
            kind = "enum";
            break;
        case FieldKind::FilePath:
            kind = "file_path";
            break;
        case FieldKind::Bool:
            kind = "bool";
            break;
        }
        result.push_back({{"path", field.key},
                          {"kind", std::move(kind)},
                          {"unit", field.unit},
                          {"enum_labels", field.enum_values},
                          {"help", field.help},
                          {"read_only", field.read_only}});
    }
    return result;
}

AgentToolResult staleEpoch(std::uint64_t current_epoch, const std::string &cause,
                           const std::vector<std::string> &undone, std::uint64_t sent_epoch) {
    std::string message = "epoch " + std::to_string(sent_epoch) + " is stale; ";
    Json details{{"epoch", current_epoch}};
    if (!cause.empty()) {
        message += cause + "; ";
        details["cause"] = cause;
    }
    message += "call circuit_get";
    if (cause == "reverted")
        details["undone"] = undone;
    AgentError error{AgentErrorCode::StaleEpoch, std::move(message)};
    error.details = std::move(details);
    return agentErrorResult(error, current_epoch);
}

AgentToolResult componentNotFound(std::uint64_t current_epoch, int component_id) {
    AgentError error{AgentErrorCode::NotFound,
                     "component " + std::to_string(component_id) + " is not in epoch " +
                         std::to_string(current_epoch) + "; call circuit_get"};
    return agentErrorResult(error, current_epoch);
}

IComponentEngine *findById(const CircuitRuntime &runtime, int component_id) {
    for (auto *component : runtime.components().all()) {
        if (component->id() == component_id)
            return component;
    }
    return nullptr;
}

Json circuitComponent(const AgentApiContext &context, const IComponentEngine &component,
                      bool include_params) {
    const auto &graph = context.runtime.graph();
    const GraphNode *node = graphNode(graph, component.graphNodeId());
    if (!node)
        return Json::object();
    const Json ports = componentPorts(context, *node);
    Json value{{"id", component.id()},         {"type", std::string(component.type_name())},
               {"label", node->label},         {"part_number", node->part_number},
               {"inputs", ports.at("inputs")}, {"outputs", ports.at("outputs")}};
    if (include_params)
        value["params"] = component.serialize();
    return value;
}

} // namespace

AgentToolResult executeCircuitReadTool(const AgentApi &api, const AgentCall &call) {
    const auto &context = api.m_context;
    AgentArgs args(call.arguments, {"include_params"});
    const bool include_params = args.optionalBool("include_params", false);
    const auto &runtime = context.runtime;

    std::vector<IComponentEngine *> components(runtime.components().all().begin(),
                                               runtime.components().all().end());
    std::sort(components.begin(), components.end(),
              [](const auto *left, const auto *right) { return left->id() < right->id(); });

    Json component_list = Json::array();
    for (const auto *component : components)
        component_list.push_back(circuitComponent(context, *component, include_params));

    const auto &network = context.network_analyzer;
    Json analyzer{{"point_a", outputEndpoint(context, network.pointAPin())},
                  {"point_b", outputEndpoint(context, network.pointBPin())},
                  {"start_Hz", agentNumber(network.startFrequency())},
                  {"stop_Hz", agentNumber(network.stopFrequency())},
                  {"points", network.points()},
                  {"stimulus_dBm", agentNumber(network.stimulusPower())}};
    Json result{{"epoch", api.epoch()},
                {"revision", context.commands.revision()},
                {"dirty", context.commands.isDirty()},
                {"project_name", nullptr},
                {"components", std::move(component_list)},
                {"probes", probeEndpoints(context)},
                {"network_analyzer", std::move(analyzer)}};
    const auto project_name = context.host.projectName();
    if (project_name)
        result["project_name"] = *project_name;

    AgentToolResult tool_result;
    tool_result.structured = std::move(result);
    return tool_result;
}

AgentToolResult executeComponentReadTool(const AgentApi &api, const AgentCall &call) {
    const auto &context = api.m_context;
    AgentArgs args(call.arguments, {"epoch", "component"});
    const std::uint64_t sent_epoch = args.requiredUInt64("epoch");
    const int component_id = args.requiredInt("component");
    if (sent_epoch != api.epoch())
        return staleEpoch(api.epoch(), api.m_last_replacement_cause, api.m_undone_summaries,
                          sent_epoch);

    IComponentEngine *component = findById(context.runtime, component_id);
    if (!component)
        return componentNotFound(api.epoch(), component_id);

    const auto &graph = context.runtime.graph();
    const GraphNode *node = graphNode(graph, component->graphNodeId());
    if (!node)
        return componentNotFound(api.epoch(), component_id);
    const Json ports = componentPorts(context, *node);
    Json result{{"epoch", api.epoch()},
                {"id", component->id()},
                {"type", std::string(component->type_name())},
                {"label", node->label},
                {"part_number", node->part_number},
                {"params", component->serialize()},
                {"param_info", parameterInfo(*component)},
                {"inputs", ports.at("inputs")},
                {"outputs", ports.at("outputs")},
                {"summary", context.runtime.components().hoverSummary(component->graphNodeId())}};
    AgentToolResult tool_result;
    tool_result.structured = std::move(result);
    return tool_result;
}
