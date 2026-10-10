#include "agent_tools.h"

#include "agent_args.h"
#include "component_type_registry.h"
#include "tool_metadata.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using Json = nlohmann::json;

const GraphNode *graphNode(const NodeGraphEngine &graph, int node_id) {
    const auto &nodes = graph.nodes();
    const auto it = std::find_if(nodes.begin(), nodes.end(), [node_id](const GraphNode &node) {
        return node.node_id == node_id;
    });
    return it == nodes.end() ? nullptr : &*it;
}

Json ports(const NodeGraphEngine &graph, const IComponentEngine &engine, bool inputs) {
    const int count = inputs ? engine.numInputPins() : engine.numOutputPins();
    const GraphNode *node = graphNode(graph, engine.graphNodeId());
    const auto *labels = node ? (inputs ? &node->input_labels : &node->output_labels) : nullptr;
    const std::string_view fallback = inputs ? "IN" : "OUT";
    Json result = Json::array();
    for (int port = 0; port < count; ++port) {
        const std::string label =
            labels && static_cast<std::size_t>(port) < labels->size() &&
                    !(*labels)[static_cast<std::size_t>(port)].empty()
                ? (*labels)[static_cast<std::size_t>(port)]
                : std::string(fallback) + (port == 0 ? "" : std::to_string(port + 1));
        result.push_back({{"port", port}, {"label", label}});
    }
    return result;
}

std::string lowercase(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value)
        result.push_back(static_cast<char>(std::tolower(character)));
    return result;
}

} // namespace

AgentToolResult executeComponentTypesTool(const AgentApi &api, const AgentCall &call) {
    AgentArgs args{call.arguments, {"type"}};
    const bool has_type = call.arguments.contains("type");
    const std::string selected_type = args.optionalString("type", {});
    const auto descriptors = ComponentTypeRegistry::instance().all();
    if (has_type && !ComponentTypeRegistry::instance().find(selected_type)) {
        AgentError error{AgentErrorCode::UnknownType, "unknown component type " + selected_type};
        return agentErrorResult(error, api.epoch());
    }

    Json types = Json::array();
    for (const auto *descriptor : descriptors) {
        if (has_type && descriptor->type != selected_type)
            continue;
        IComponentEngine *engine = api.scratchTypeEngine(descriptor->type);
        if (!engine)
            continue;
        Json type{{"type", descriptor->type},
                  {"display_name", descriptor->display_name},
                  {"label_prefix", descriptor->label_prefix},
                  {"inputs", ports(api.m_type_graph, *engine, true)},
                  {"outputs", ports(api.m_type_graph, *engine, false)}};
        if (has_type) {
            type["default_params"] = engine->serialize();
            type["param_info"] = agent_api_detail::parameterInfo(*descriptor);
        }
        types.push_back(std::move(type));
    }

    AgentToolResult result;
    result.structured = {{"epoch", api.epoch()}, {"types", std::move(types)}};
    return result;
}

AgentToolResult executeLibrarySearchTool(const AgentApi &api, const AgentCall &call) {
    AgentArgs args{call.arguments, {"query", "type", "limit"}};
    const std::string query = args.optionalString("query", {});
    const std::string type = args.optionalString("type", {});
    const int limit = args.optionalInt("limit", 20, 1, 50);
    const std::string normalized_query = lowercase(query);

    std::vector<const ComponentDefinition *> matches;
    for (const auto *definition : api.m_context.library.all()) {
        if (!type.empty() && definition->type != type)
            continue;
        if (lowercase(definition->part_number).find(normalized_query) != std::string::npos ||
            lowercase(definition->manufacturer).find(normalized_query) != std::string::npos ||
            lowercase(definition->description).find(normalized_query) != std::string::npos)
            matches.push_back(definition);
    }
    std::sort(matches.begin(), matches.end(), [](const auto *left, const auto *right) {
        return std::tie(left->type, left->part_number) < std::tie(right->type, right->part_number);
    });

    const auto total = matches.size();
    Json parts = Json::array();
    const auto result_count = std::min(total, static_cast<std::size_t>(limit));
    for (std::size_t index = 0; index < result_count; ++index) {
        const auto &definition = *matches[index];
        parts.push_back({{"part_number", definition.part_number},
                         {"type", definition.type},
                         {"manufacturer", definition.manufacturer},
                         {"description", definition.description},
                         {"parameters", definition.parameters},
                         {"has_data_files", !definition.data_files.empty()}});
    }
    AgentToolResult result;
    result.structured = {{"epoch", api.epoch()}, {"parts", std::move(parts)}, {"total", total}};
    return result;
}
