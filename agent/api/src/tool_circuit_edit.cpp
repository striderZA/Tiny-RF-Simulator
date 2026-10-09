#include "agent_tools.h"

#include "agent_args.h"
#include "agent_links.h"
#include "agent_placement.h"
#include "component_type_registry.h"
#include "logging_core.h"
#include "tool_metadata.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

std::string childPath(std::string_view base, std::string_view key) {
    std::string result{base};
    result += '/';
    for (const char character : key) {
        if (character == '~')
            result += "~0";
        else if (character == '/')
            result += "~1";
        else
            result += character;
    }
    return result;
}

[[noreturn]] void fail(AgentError error) { throw AgentArgumentError(std::move(error)); }

[[noreturn]] void invalid(std::string path, std::string message) {
    AgentError error{AgentErrorCode::InvalidArgument, std::move(message)};
    error.details = {{"path", std::move(path)}};
    fail(std::move(error));
}

[[noreturn]] void notFound(std::string message) {
    fail(AgentError{AgentErrorCode::NotFound, std::move(message)});
}

const OrderedJson &required(const OrderedJson &object, std::string_view key,
                            std::string_view path) {
    const auto found = object.find(std::string(key));
    if (found == object.end())
        invalid(childPath(path, key), "required field is missing");
    return *found;
}

void requireObject(const OrderedJson &value, std::string_view path) {
    if (!value.is_object())
        invalid(std::string(path), "expected an object");
}

void exactKeys(const OrderedJson &value, std::string_view path,
               std::initializer_list<std::string_view> allowed) {
    requireObject(value, path);
    for (auto it = value.begin(); it != value.end(); ++it) {
        const bool known = std::find(allowed.begin(), allowed.end(), it.key()) != allowed.end();
        if (!known)
            invalid(childPath(path, it.key()), "unknown field");
    }
}

std::string stringValue(const OrderedJson &value, std::string_view path) {
    if (!value.is_string())
        invalid(std::string(path), "expected a string");
    return value.get<std::string>();
}

std::uint64_t unsignedValue(const OrderedJson &value, std::string_view path) {
    if (value.is_number_unsigned())
        return value.get<std::uint64_t>();
    if (value.is_number_integer()) {
        const auto number = value.get<std::int64_t>();
        if (number >= 0)
            return static_cast<std::uint64_t>(number);
    }
    invalid(std::string(path), "expected a non-negative integer");
}

int intValue(const OrderedJson &value, std::string_view path) {
    const std::uint64_t number = unsignedValue(value, path);
    if (number > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        invalid(std::string(path), "integer is out of range");
    return static_cast<int>(number);
}

bool validRef(std::string_view ref) {
    if (ref.empty() || ref.size() > 32 ||
        !((ref.front() >= 'A' && ref.front() <= 'Z') || (ref.front() >= 'a' && ref.front() <= 'z')))
        return false;
    return std::all_of(ref.begin() + 1, ref.end(), [](char character) {
        return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '_';
    });
}

std::string lower(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const unsigned char character : text)
        result.push_back(static_cast<char>(std::tolower(character)));
    return result;
}

IComponentEngine *findComponentById(const CircuitRuntime &runtime, int component_id) {
    for (auto *component : runtime.components().all()) {
        if (component->id() == component_id)
            return component;
    }
    return nullptr;
}

const GraphNode *findGraphNode(const NodeGraphEngine &graph, int node_id) {
    const auto &nodes = graph.nodes();
    const auto found = std::find_if(nodes.begin(), nodes.end(), [node_id](const GraphNode &node) {
        return node.node_id == node_id;
    });
    return found == nodes.end() ? nullptr : &*found;
}

int portCount(const NodeGraphEngine &graph, const IComponentEngine &component, bool output) {
    const GraphNode *node = findGraphNode(graph, component.graphNodeId());
    if (!node)
        return output ? component.numOutputPins() : component.numInputPins();
    return static_cast<int>(output ? node->output_pin_ids.size() : node->input_pin_ids.size());
}

std::string portCounts(const NodeGraphEngine &graph, const IComponentEngine &component) {
    const int inputs = portCount(graph, component, false);
    const int outputs = portCount(graph, component, true);
    return std::to_string(inputs) + (inputs == 1 ? " input" : " inputs") + " and " +
           std::to_string(outputs) + (outputs == 1 ? " output" : " outputs");
}

struct Endpoint {
    IComponentEngine *component = nullptr;
    int port = -1;
    int pin = -1;
};

struct RefEntry {
    std::string name;
    int component_id = -1;
};

struct EditSummary {
    int components_added = 0;
    int components_removed = 0;
    int links_added = 0;
    int links_removed = 0;
    int parameter_changes = 0;
    int probes_added = 0;
    int probes_removed = 0;

    std::string text() const {
        std::vector<std::string> pieces;
        const auto append = [&pieces](int count, std::string_view singular, std::string_view plural,
                                      std::string_view prefix = {}) {
            if (count == 0)
                return;
            std::string part{prefix};
            part += std::to_string(count);
            part += ' ';
            part += count == 1 ? singular : plural;
            pieces.push_back(std::move(part));
        };
        append(components_added, "component", "components", "+");
        append(components_removed, "component", "components", "-");
        append(links_added, "link", "links", "+");
        append(links_removed, "link", "links", "-");
        append(parameter_changes, "parameter change", "parameter changes");
        append(probes_added, "probe", "probes", "+");
        append(probes_removed, "probe", "probes", "-");

        std::string result;
        for (const auto &piece : pieces) {
            if (!result.empty())
                result += ", ";
            result += piece;
        }
        return result;
    }
};

AgentToolResult errorWithPrefix(AgentError error, std::uint64_t epoch, const Json &applied) {
    AgentToolResult result = agentErrorResult(error, epoch);
    result.structured["applied"] = applied;
    return result;
}

void requireOperationKeys(const OrderedJson &operation, std::string_view path,
                          std::initializer_list<std::string_view> allowed) {
    exactKeys(operation, path, allowed);
    (void)required(operation, "op", path);
}

IComponentEngine *resolveComponent(const OrderedJson &object, std::string_view base_path,
                                   const CircuitRuntime &runtime,
                                   const std::unordered_map<std::string, int> &refs) {
    const OrderedJson *id_value = nullptr;
    const OrderedJson *ref_value = nullptr;
    const auto component_it = object.find("component");
    const auto ref_it = object.find("ref");
    if (component_it != object.end())
        id_value = &*component_it;
    if (ref_it != object.end())
        ref_value = &*ref_it;
    if ((id_value == nullptr) == (ref_value == nullptr))
        invalid(std::string(base_path), "provide exactly one of component or ref");

    int component_id = -1;
    if (id_value) {
        component_id = intValue(*id_value, childPath(base_path, "component"));
    } else {
        const std::string ref = stringValue(*ref_value, childPath(base_path, "ref"));
        if (!validRef(ref))
            invalid(childPath(base_path, "ref"), "ref must match [A-Za-z][A-Za-z0-9_]{0,31}");
        const auto found = refs.find(ref);
        if (found == refs.end())
            notFound("ref " + ref + " is not defined by an earlier add");
        component_id = found->second;
    }
    IComponentEngine *component = findComponentById(runtime, component_id);
    if (!component)
        notFound("component " + std::to_string(component_id) + " is not in this circuit");
    return component;
}

Endpoint parseEndpoint(const OrderedJson &value, std::string_view path, bool output,
                       const CircuitRuntime &runtime,
                       const std::unordered_map<std::string, int> &refs) {
    exactKeys(value, path, {"component", "ref", "port"});
    const OrderedJson &port_value = required(value, "port", path);
    IComponentEngine *component = resolveComponent(value, path, runtime, refs);
    const int port = intValue(port_value, childPath(path, "port"));
    const NodeGraphEngine &graph = runtime.graph();
    const int count = portCount(graph, *component, output);
    if (port >= count) {
        notFound("component " + std::to_string(component->id()) + " has " +
                 portCounts(graph, *component) + "; port " + std::to_string(port) +
                 " does not exist");
    }
    const int pin = output ? component->outputPinId(port) : component->inputPinId(port);
    if (pin < 0) {
        notFound("component " + std::to_string(component->id()) + " has " +
                 portCounts(graph, *component) + "; port " + std::to_string(port) +
                 " does not exist");
    }
    return {component, port, pin};
}

std::string labelFor(const NodeGraphEngine &graph, const IComponentEngine &component) {
    const GraphNode *node = findGraphNode(graph, component.graphNodeId());
    return node ? node->label : std::string{};
}

AgentError paramError(const ParamWriteResult &result, int op_index) {
    return agentParamError(result, op_index);
}

Json changedValues(const std::vector<ParamChange> &changes) {
    Json result = Json::array();
    for (const auto &change : changes) {
        result.push_back({{"path", change.path},
                          {"old_value", change.old_value},
                          {"new_value", change.new_value}});
    }
    return result;
}

Json sourceForOccupiedInput(const AgentApiContext &context, int end_pin) {
    const NodeGraphEngine &graph = context.runtime.graph();
    for (const auto &link : graph.links()) {
        if (link.end_pin_id != end_pin)
            continue;
        const int source_node_id = graph.nodeIdForPin(link.start_pin_id);
        IComponentEngine *component = context.runtime.components().find(source_node_id);
        const GraphNode *node = findGraphNode(graph, source_node_id);
        if (!component || !node)
            return nullptr;
        const auto pin =
            std::find(node->output_pin_ids.begin(), node->output_pin_ids.end(), link.start_pin_id);
        if (pin == node->output_pin_ids.end())
            return nullptr;
        return Json{{"component", component->id()},
                    {"port", static_cast<int>(std::distance(node->output_pin_ids.begin(), pin))}};
    }
    return nullptr;
}

std::vector<ComponentDefinition> matchingParts(const ComponentLibrary &library,
                                               std::string_view part_number, std::string_view type,
                                               bool type_supplied) {
    std::vector<ComponentDefinition> matches;
    const std::string wanted = lower(part_number);
    for (const ComponentDefinition *definition : library.all()) {
        if (lower(definition->part_number) != wanted)
            continue;
        if (type_supplied && definition->type != type)
            continue;
        matches.push_back(*definition);
    }
    std::sort(matches.begin(), matches.end(),
              [](const ComponentDefinition &left, const ComponentDefinition &right) {
                  return std::tie(left.part_number, left.type, left.manufacturer) <
                         std::tie(right.part_number, right.type, right.manufacturer);
              });
    return matches;
}

Json partCandidates(const ComponentLibrary &library, std::string_view part_number) {
    std::vector<const ComponentDefinition *> candidates;
    const std::string wanted = lower(part_number);
    for (const ComponentDefinition *definition : library.all()) {
        const std::string candidate = lower(definition->part_number);
        if (candidate.find(wanted) == std::string::npos)
            continue;
        candidates.push_back(definition);
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto *left, const auto *right) {
        return std::tie(left->part_number, left->type, left->manufacturer) <
               std::tie(right->part_number, right->type, right->manufacturer);
    });
    if (candidates.size() > 5)
        candidates.resize(5);
    Json result = Json::array();
    for (const ComponentDefinition *candidate : candidates) {
        result.push_back({{"part_number", candidate->part_number},
                          {"type", candidate->type},
                          {"manufacturer", candidate->manufacturer}});
    }
    return result;
}

Json componentRefJson(const std::vector<RefEntry> &refs) {
    Json result = Json::object();
    for (const auto &entry : refs)
        result[entry.name] = entry.component_id;
    return result;
}

AgentToolResult successResult(std::uint64_t epoch, std::uint64_t revision, Json applied,
                              Json refs) {
    AgentToolResult result;
    result.structured = {{"epoch", epoch},
                         {"revision", revision},
                         {"applied", std::move(applied)},
                         {"refs", std::move(refs)}};
    return result;
}

void validateParams(const OrderedJson &params, std::string_view path) {
    if (!params.is_object())
        invalid(std::string(path), "expected an object");
}

std::optional<std::array<float, 2>> parsePosition(const OrderedJson &operation,
                                                  std::string_view operation_path) {
    const auto position_it = operation.find("position");
    if (position_it == operation.end())
        return std::nullopt;
    const std::string path = childPath(operation_path, "position");
    exactKeys(*position_it, path, {"x", "y"});
    const OrderedJson &x_value = required(*position_it, "x", path);
    const OrderedJson &y_value = required(*position_it, "y", path);
    if (!x_value.is_number() || !y_value.is_number())
        invalid(path, "position coordinates must be finite numbers");
    const double x = x_value.get<double>();
    const double y = y_value.get<double>();
    const float x_position = static_cast<float>(x);
    const float y_position = static_cast<float>(y);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(x_position) ||
        !std::isfinite(y_position))
        invalid(path, "position coordinates must be finite numbers");
    return std::array<float, 2>{x_position, y_position};
}

} // namespace

AgentError agentParamError(const ParamWriteResult &result, int op_index) {
    AgentError error{AgentErrorCode::Internal, "parameter write failed"};
    error.op_index = op_index;
    const auto statusName = [](ParamWriteStatus status) -> std::string_view {
        switch (status) {
        case ParamWriteStatus::Applied:
            return "APPLIED";
        case ParamWriteStatus::Unchanged:
            return "UNCHANGED";
        case ParamWriteStatus::UnknownComponent:
            return "UNKNOWN_COMPONENT";
        case ParamWriteStatus::UnknownKey:
            return "UNKNOWN_KEY";
        case ParamWriteStatus::PathParamUnsupported:
            return "PATH_PARAM_UNSUPPORTED";
        case ParamWriteStatus::ReadOnly:
            return "READ_ONLY";
        case ParamWriteStatus::TypeMismatch:
            return "TYPE_MISMATCH";
        case ParamWriteStatus::EngineAdjusted:
            return "ENGINE_ADJUSTED";
        case ParamWriteStatus::DeserializeFailed:
            return "DESERIALIZE_FAILED";
        case ParamWriteStatus::RestoreFailed:
            return "RESTORE_FAILED";
        }
        return "INTERNAL";
    };

    const std::string_view reason = statusName(result.status);
    if (result.status == ParamWriteStatus::UnknownComponent) {
        error.code = AgentErrorCode::NotFound;
        error.message = "component for parameter write was not found";
    } else if (result.status == ParamWriteStatus::PathParamUnsupported) {
        error.code = AgentErrorCode::PathParamsUnsupported;
        error.message = "path parameters are unsupported by agent tools";
        if (!result.path.empty())
            error.details["path"] = result.path;
    } else if (result.status == ParamWriteStatus::RestoreFailed) {
        error.code = AgentErrorCode::Internal;
        error.message = "parameter write rollback failed";
        error.details["reason"] = "RESTORE_FAILED";
        LOG_ERROR("Agent parameter rollback failed: %s", result.error.c_str());
    } else {
        error.code = AgentErrorCode::ParamRejected;
        error.message = "parameter write rejected";
        error.details["reason"] = std::string(reason);
        if (!result.path.empty())
            error.details["path"] = result.path;
        if (!result.suggestions.empty())
            error.details["suggestions"] = result.suggestions;
        if (!result.expected.empty())
            error.details["expected"] = result.expected;
        if (result.status == ParamWriteStatus::EngineAdjusted) {
            error.details["requested"] = result.requested;
            error.details["stored"] = result.stored;
        }
    }
    return error;
}

AgentToolResult executeCircuitEditTool(const AgentApi &api, const AgentCall &call) {
    const AgentApiContext &context = api.m_context;
    AgentArgs args(call.arguments, {"epoch", "ops"});
    const std::uint64_t sent_epoch = args.requiredUInt64("epoch");
    const auto ops_it = call.arguments.find("ops");
    if (ops_it == call.arguments.end())
        invalid("/ops", "required field is missing");
    if (!ops_it->is_array())
        invalid("/ops", "expected an array");
    if (ops_it->empty() || ops_it->size() > 64)
        invalid("/ops", "expected between 1 and 64 operations");
    const std::uint64_t epoch = api.epoch();
    if (sent_epoch != epoch) {
        std::string message = "epoch " + std::to_string(sent_epoch) + " is stale; ";
        Json details{{"epoch", epoch}};
        if (!api.m_last_replacement_cause.empty()) {
            message += api.m_last_replacement_cause + "; ";
            details["cause"] = api.m_last_replacement_cause;
        }
        message += "call circuit_get";
        if (api.m_last_replacement_cause == "reverted")
            details["undone"] = api.m_undone_summaries;
        AgentError error{AgentErrorCode::StaleEpoch, std::move(message)};
        error.details = std::move(details);
        return agentErrorResult(error, epoch);
    }

    context.host.beginCheckpoint(call);
    Json applied = Json::array();
    std::unordered_map<std::string, int> refs_by_name;
    std::vector<RefEntry> refs;
    std::vector<int> added_node_ids;
    std::vector<std::optional<std::array<float, 2>>> positions;
    EditSummary summary;

    const auto finish = [&](bool preserve_empty_checkpoint = false) {
        if (applied.empty() && !preserve_empty_checkpoint) {
            context.host.discardCheckpoint();
        } else {
            context.host.commitCheckpoint(summary.text());
            if (!added_node_ids.empty())
                context.host.placeComponents(
                    planAgentPlacement(added_node_ids, positions, context.runtime.graph()));
        }
    };

    const auto process = [&](std::size_t index, const OrderedJson &operation) {
        const std::string operation_path = "/ops/" + std::to_string(index);
        requireObject(operation, operation_path);
        const OrderedJson &op_value = required(operation, "op", operation_path);
        const std::string op = stringValue(op_value, childPath(operation_path, "op"));
        const auto ref_it = operation.find("ref");
        Json applied_entry{{"op_index", index}, {"op", op}};

        if (op == "add") {
            requireOperationKeys(operation, operation_path,
                                 {"op", "ref", "type", "library_part", "params", "position"});
            std::optional<std::string> ref;
            if (ref_it != operation.end()) {
                ref = stringValue(*ref_it, childPath(operation_path, "ref"));
                if (!validRef(*ref))
                    invalid(childPath(operation_path, "ref"),
                            "ref must match [A-Za-z][A-Za-z0-9_]{0,31}");
                if (refs_by_name.contains(*ref))
                    invalid(childPath(operation_path, "ref"),
                            "ref is already defined in this call");
            }

            const auto type_it = operation.find("type");
            const auto library_it = operation.find("library_part");
            if ((type_it == operation.end()) == (library_it == operation.end()))
                invalid(operation_path, "add requires exactly one of type or library_part");
            OrderedJson params = OrderedJson::object();
            const auto params_it = operation.find("params");
            if (params_it != operation.end()) {
                validateParams(*params_it, childPath(operation_path, "params"));
                params = *params_it;
            }
            const auto position = parsePosition(operation, operation_path);

            IComponentEngine *component = nullptr;
            if (type_it != operation.end()) {
                const std::string type = stringValue(*type_it, childPath(operation_path, "type"));
                const ComponentTypeDescriptor *descriptor =
                    ComponentTypeRegistry::instance().find(type);
                if (!descriptor) {
                    AgentError error{AgentErrorCode::UnknownType, "unknown component type " + type};
                    fail(std::move(error));
                }
                if (params_it == operation.end()) {
                    component = context.commands.createComponent(descriptor->create);
                } else {
                    const ComponentAddResult created =
                        context.commands.createComponentWithParams(descriptor->create, params);
                    component = created.component;
                    if (!component) {
                        if (!created.params.ok())
                            fail(agentParamError(created.params, static_cast<int>(index)));
                        fail(AgentError{AgentErrorCode::Internal, "could not create component"});
                    }
                }
            } else {
                const std::string library_path = childPath(operation_path, "library_part");
                const OrderedJson &library_part = *library_it;
                exactKeys(library_part, library_path, {"part_number", "type"});
                const std::string part_number =
                    stringValue(required(library_part, "part_number", library_path),
                                childPath(library_path, "part_number"));
                std::string selected_type;
                const auto requested_type = library_part.find("type");
                const bool type_supplied = requested_type != library_part.end();
                if (type_supplied)
                    selected_type = stringValue(*requested_type, childPath(library_path, "type"));
                const auto candidates =
                    matchingParts(context.library, part_number, selected_type, type_supplied);
                if (candidates.empty()) {
                    AgentError error{AgentErrorCode::UnknownPart,
                                     "unknown library part " + part_number};
                    error.details = {{"candidates", partCandidates(context.library, part_number)}};
                    fail(std::move(error));
                }
                if (candidates.size() > 1) {
                    Json ambiguity_candidates = Json::array();
                    for (const auto &candidate : candidates) {
                        ambiguity_candidates.push_back({{"part_number", candidate.part_number},
                                                        {"type", candidate.type},
                                                        {"manufacturer", candidate.manufacturer}});
                    }
                    AgentError error{AgentErrorCode::AmbiguousPart,
                                     "library part " + part_number + " is ambiguous"};
                    error.details = {{"candidates", std::move(ambiguity_candidates)}};
                    fail(std::move(error));
                }
                const ComponentAddResult created =
                    context.commands.addLibraryPart(context.library, candidates.front(), params);
                component = created.component;
                if (!component) {
                    if (!created.params.ok())
                        fail(agentParamError(created.params, static_cast<int>(index)));
                    fail(AgentError{AgentErrorCode::Internal,
                                    "could not instantiate library part " + part_number});
                }
            }
            if (!component)
                fail(AgentError{AgentErrorCode::Internal, "could not create component"});

            const std::string label = labelFor(context.runtime.graph(), *component);
            applied_entry["component"] = component->id();
            if (ref) {
                applied_entry["ref"] = *ref;
                refs_by_name.emplace(*ref, component->id());
                refs.push_back({*ref, component->id()});
            }
            applied_entry["label"] = label;
            applied_entry["params"] = component->serialize();
            applied.push_back(std::move(applied_entry));
            added_node_ids.push_back(component->graphNodeId());
            positions.push_back(position);
            ++summary.components_added;
            return;
        }

        if (op == "remove") {
            requireOperationKeys(operation, operation_path, {"op", "component"});
            const int component_id = intValue(required(operation, "component", operation_path),
                                              childPath(operation_path, "component"));
            IComponentEngine *component = findComponentById(context.runtime, component_id);
            if (!component)
                notFound("component " + std::to_string(component_id) + " is not in this circuit");
            const int graph_node_id = component->graphNodeId();
            const std::string label = labelFor(context.runtime.graph(), *component);
            const std::size_t old_links = context.runtime.graph().links().size();
            const std::size_t old_probes = context.runtime.graph().probePins().size();
            if (!context.commands.removeComponent(graph_node_id))
                notFound("component " + std::to_string(component_id) + " is not in this circuit");
            const std::size_t new_links = context.runtime.graph().links().size();
            const std::size_t new_probes = context.runtime.graph().probePins().size();
            summary.links_removed += static_cast<int>(old_links - new_links);
            summary.probes_removed += static_cast<int>(old_probes - new_probes);
            ++summary.components_removed;
            applied_entry["component"] = component_id;
            applied_entry["label"] = label;
            applied.push_back(std::move(applied_entry));
            for (std::size_t added_index = 0; added_index < added_node_ids.size();) {
                if (added_node_ids[added_index] == graph_node_id) {
                    added_node_ids.erase(added_node_ids.begin() +
                                         static_cast<std::ptrdiff_t>(added_index));
                    positions.erase(positions.begin() + static_cast<std::ptrdiff_t>(added_index));
                } else {
                    ++added_index;
                }
            }
            return;
        }

        if (op == "set_params") {
            requireOperationKeys(operation, operation_path, {"op", "component", "ref", "params"});
            const OrderedJson &params_value = required(operation, "params", operation_path);
            validateParams(params_value, childPath(operation_path, "params"));
            IComponentEngine *component =
                resolveComponent(operation, operation_path, context.runtime, refs_by_name);
            const ParamWriteResult write =
                context.commands.setComponentParams(component->graphNodeId(), params_value);
            if (!write.ok())
                fail(paramError(write, static_cast<int>(index)));
            applied_entry["component"] = component->id();
            applied_entry["params"] = component->serialize();
            if (!write.also_changed.empty())
                applied_entry["also_changed"] = changedValues(write.also_changed);
            if (write.status == ParamWriteStatus::Unchanged) {
                applied_entry["unchanged"] = true;
            } else {
                ++summary.parameter_changes;
            }
            applied.push_back(std::move(applied_entry));
            return;
        }

        if (op == "connect") {
            requireOperationKeys(operation, operation_path, {"op", "from", "to"});
            const OrderedJson &from_value = required(operation, "from", operation_path);
            const OrderedJson &to_value = required(operation, "to", operation_path);
            const Endpoint source = parseEndpoint(from_value, childPath(operation_path, "from"),
                                                  true, context.runtime, refs_by_name);
            const Endpoint target = parseEndpoint(to_value, childPath(operation_path, "to"), false,
                                                  context.runtime, refs_by_name);
            if (!context.commands.connect(source.pin, target.pin)) {
                const std::string reason =
                    classifyLinkRejection(context.runtime.graph(), *source.component,
                                          *target.component, source.pin, target.pin);
                AgentError error{AgentErrorCode::LinkRejected, "connection rejected: " + reason};
                error.details = {{"reason", reason}};
                if (reason == "INPUT_OCCUPIED")
                    error.details["existing_source"] = sourceForOccupiedInput(context, target.pin);
                fail(std::move(error));
            }
            ++summary.links_added;
            applied_entry["component"] = target.component->id();
            applied.push_back(std::move(applied_entry));
            return;
        }

        if (op == "disconnect") {
            requireOperationKeys(operation, operation_path, {"op", "to"});
            const Endpoint target = parseEndpoint(required(operation, "to", operation_path),
                                                  childPath(operation_path, "to"), false,
                                                  context.runtime, refs_by_name);
            const auto &links = context.runtime.graph().links();
            const auto link =
                std::find_if(links.begin(), links.end(), [&target](const GraphLink &item) {
                    return item.end_pin_id == target.pin;
                });
            if (link == links.end())
                notFound("input port is not connected");
            if (!context.commands.disconnect(link->link_id))
                notFound("input port is not connected");
            ++summary.links_removed;
            applied_entry["component"] = target.component->id();
            applied.push_back(std::move(applied_entry));
            return;
        }

        if (op == "probe_add" || op == "probe_remove") {
            requireOperationKeys(operation, operation_path, {"op", "at"});
            const Endpoint endpoint =
                parseEndpoint(required(operation, "at", operation_path),
                              childPath(operation_path, "at"), true, context.runtime, refs_by_name);
            const auto &pins = context.runtime.graph().probePins();
            const bool already_probed =
                std::find(pins.begin(), pins.end(), endpoint.pin) != pins.end();
            if (op == "probe_add") {
                if (already_probed) {
                    applied_entry["component"] = endpoint.component->id();
                    applied_entry["unchanged"] = true;
                    applied.push_back(std::move(applied_entry));
                    return;
                }
                if (pins.size() >= static_cast<std::size_t>(NodeGraphEngine::MAX_PROBES)) {
                    AgentError error{AgentErrorCode::InvalidArgument,
                                     "probe limit of " +
                                         std::to_string(NodeGraphEngine::MAX_PROBES) + " reached"};
                    error.details = {{"limit", NodeGraphEngine::MAX_PROBES}};
                    fail(std::move(error));
                }
                if (!context.commands.addProbePin(endpoint.pin))
                    notFound("output port could not be probed");
                ++summary.probes_added;
            } else {
                if (!already_probed)
                    notFound("output port is not probed");
                if (!context.commands.removeProbePin(endpoint.pin))
                    notFound("output port is not probed");
                ++summary.probes_removed;
            }
            applied_entry["component"] = endpoint.component->id();
            applied.push_back(std::move(applied_entry));
            return;
        }

        invalid(childPath(operation_path, "op"), "unknown operation " + op);
    };

    for (std::size_t index = 0; index < ops_it->size(); ++index) {
        try {
            process(index, (*ops_it)[index]);
        } catch (const AgentArgumentError &argument_error) {
            AgentError error = argument_error.error();
            error.op_index = static_cast<int>(index);
            const bool preserve_empty_checkpoint =
                error.details.value("reason", std::string{}) == "RESTORE_FAILED";
            finish(preserve_empty_checkpoint);
            return errorWithPrefix(std::move(error), api.epoch(), applied);
        }
    }

    finish();
    return successResult(api.epoch(), context.commands.revision(), std::move(applied),
                         componentRefJson(refs));
}
