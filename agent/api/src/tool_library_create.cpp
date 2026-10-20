#include "agent_tools.h"

#include "agent_args.h"
#include "component_type_registry.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

[[noreturn]] void invalidArgument(std::string path, std::string message) {
    AgentError error{AgentErrorCode::InvalidArgument, std::move(message)};
    error.details = {{"path", std::move(path)}};
    throw AgentArgumentError(std::move(error));
}

std::string requiredString(const OrderedJson &arguments, std::string_view key) {
    const auto found = arguments.find(std::string(key));
    const std::string path = "/" + std::string(key);
    if (found == arguments.end())
        invalidArgument(path, "required argument is missing");
    if (!found->is_string())
        invalidArgument(path, "expected a string");
    return found->get<std::string>();
}

std::string pointerField(std::string_view field) {
    std::string path = "/parameters/";
    for (const char character : field) {
        if (character == '~')
            path += "~0";
        else if (character == '/')
            path += "~1";
        else
            path += character;
    }
    return path;
}

AgentToolResult definitionError(const AgentApi &api, const ValidationIssue &issue) {
    std::string path;
    if (issue.field == "type" || issue.field == "part_number" || issue.field == "parameters" ||
        issue.field == "data_files") {
        path = "/" + issue.field;
    } else {
        path = pointerField(issue.field);
    }
    AgentError error{AgentErrorCode::InvalidArgument, issue.message};
    error.details = {{"path", std::move(path)}};
    return agentErrorResult(error, api.epoch());
}

AgentToolResult internalWriteError(const AgentApi &api, std::string_view message) {
    AgentError error{AgentErrorCode::Internal, std::string(message)};
    return agentErrorResult(error, api.epoch());
}

} // namespace

AgentToolResult executeLibraryCreateTool(const AgentApi &api, const AgentCall &call) {
    AgentArgs args{call.arguments,
                   {"type", "part_number", "manufacturer", "description", "notes", "parameters",
                    "scope"}};
    const std::string type = requiredString(call.arguments, "type");
    const std::string part_number = requiredString(call.arguments, "part_number");
    const std::string scope = args.optionalString("scope", "project");
    if (scope != "project" && scope != "global")
        invalidArgument("/scope", "expected 'project' or 'global'");

    const ComponentTypeDescriptor *descriptor = ComponentTypeRegistry::instance().find(type);
    if (!descriptor) {
        AgentError error{AgentErrorCode::UnknownType, "unknown component type " + type};
        error.details = {{"path", "/type"}};
        return agentErrorResult(error, api.epoch());
    }
    if (!descriptor->authorable)
        invalidArgument("/type", "component type does not support library authoring");

    const auto parameter_it = call.arguments.find("parameters");
    if (parameter_it == call.arguments.end())
        invalidArgument("/parameters", "required argument is missing");
    if (!parameter_it->is_object())
        invalidArgument("/parameters", "expected an object");

    const auto root = api.m_context.host.componentLibraryRoot(scope);
    if (!root)
        return internalWriteError(api, "component library destination is unavailable");

    ComponentDefinition definition{};
    definition.schema_version = 2;
    definition.type = type;
    definition.part_number = part_number;
    definition.manufacturer = args.optionalString("manufacturer", {});
    definition.description = args.optionalString("description", {});
    definition.notes = args.optionalString("notes", {});
    definition.parameters = *parameter_it;

    ComponentLibraryCreateResult created =
        api.m_context.library.createUserEntry(std::move(definition), *root);
    if (created.status == ComponentLibraryCreateStatus::InvalidDefinition) {
        if (created.issues.empty())
            return internalWriteError(api, "component definition validation returned no details");
        return definitionError(api, created.issues.front());
    }
    if (created.status == ComponentLibraryCreateStatus::AlreadyExists) {
        AgentError error{AgentErrorCode::InvalidArgument, created.message};
        error.details = {{"path", "/part_number"}};
        return agentErrorResult(error, api.epoch());
    }
    if (created.status != ComponentLibraryCreateStatus::Created)
        return internalWriteError(api, "could not create the component-library entry");

    const std::string relative_path =
        created.path.lexically_relative(std::filesystem::path(*root)).generic_string();
    AgentToolResult result;
    result.structured = {{"epoch", api.epoch()},
                         {"scope", scope},
                         {"library_path", relative_path},
                         {"part_number", part_number},
                         {"type", type},
                         {"manufacturer", args.optionalString("manufacturer", {})}};
    return result;
}
