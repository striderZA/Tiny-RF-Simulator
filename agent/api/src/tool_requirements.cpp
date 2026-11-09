#include "agent_args.h"
#include "agent_tools.h"
#include "receiver_requirements.h"

#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

[[noreturn]] void fail(AgentError error) { throw AgentArgumentError(std::move(error)); }

[[noreturn]] void invalid(std::string path, std::string message) {
    AgentError error{AgentErrorCode::InvalidArgument, std::move(message)};
    error.details = {{"path", std::move(path)}};
    fail(std::move(error));
}

// The epoch every edit carries. It is judged before any other argument, so a stale call reports
// STALE_EPOCH even when its other arguments are also invalid.
std::uint64_t requiredEpoch(const OrderedJson &arguments) {
    if (!arguments.is_object())
        invalid("/", "expected an object");
    const auto found = arguments.find("epoch");
    if (found == arguments.end())
        invalid("/epoch", "required argument is missing");
    if (found->is_number_unsigned())
        return found->get<std::uint64_t>();
    if (found->is_number_integer() && found->get<std::int64_t>() >= 0)
        return static_cast<std::uint64_t>(found->get<std::int64_t>());
    invalid("/epoch", "expected a non-negative integer");
}

// Unknown top-level argument keys, matching the schema's additionalProperties.
void checkArgumentKeys(const OrderedJson &arguments) {
    if (!arguments.is_object())
        invalid("/", "expected an object");
    for (const auto &member : arguments.items()) {
        const std::string_view key = member.key();
        if (key != "epoch" && key != "requirements")
            invalid("/" + std::string(key), "unknown field");
    }
}

AgentToolResult unavailable(const AgentApi &api) {
    return agentErrorResult(
        AgentError{AgentErrorCode::Internal, "receiver requirements are unavailable"}, api.epoch());
}

// The success shape shared by both tools. A malformed retained set has no modes to encode, so the
// empty format-2 object is sent and the diagnostic travels in `invalid_reason`.
AgentToolResult requirementsResult(std::uint64_t epoch, std::uint64_t revision, bool dirty,
                                   const ReceiverRequirementsSet &set) {
    AgentToolResult result;
    result.structured = Json{
        {"epoch", epoch},
        {"revision", revision},
        {"dirty", dirty},
        {"requirements",
         receiverRequirementsToJson(set.invalid_reason.empty() ? set : ReceiverRequirementsSet{})},
        {"invalid_reason", set.invalid_reason.empty() ? Json(nullptr) : Json(set.invalid_reason)}};
    return result;
}

} // namespace

AgentToolResult executeRequirementsGetTool(const AgentApi &api, const AgentCall &call) {
    // requirements_get takes no arguments; AgentArgs rejects an object carrying any.
    AgentArgs args(call.arguments, {});
    const std::optional<ReceiverRequirementsSet> set = api.m_context.host.receiverRequirements();
    if (!set)
        return unavailable(api);
    return requirementsResult(api.epoch(), api.m_context.commands.revision(),
                              api.m_context.commands.isDirty(), *set);
}

AgentToolResult executeRequirementsEditTool(const AgentApi &api, const AgentCall &call) {
    const std::uint64_t epoch = api.epoch();
    if (requiredEpoch(call.arguments) != epoch)
        return staleMeasurementEpochError(epoch, api.m_last_replacement_cause,
                                          api.m_undone_summaries);
    checkArgumentKeys(call.arguments);

    const auto found = call.arguments.find("requirements");
    if (found == call.arguments.end())
        invalid("/requirements", "required field is missing");
    if (!found->is_object())
        invalid("/requirements", "expected an object");

    const auto format = found->find("format");
    if (format == found->end())
        invalid("/requirements/format", "required field is missing");
    if (!format->is_number_integer() && !format->is_number_unsigned())
        invalid("/requirements/format", "expected " + std::to_string(kReceiverRequirementsFormat));
    if (format->get<long long>() != kReceiverRequirementsFormat)
        invalid("/requirements/format", "expected " + std::to_string(kReceiverRequirementsFormat));

    ReceiverRequirementsSet set = receiverRequirementsFromJson(*found);
    if (!set.invalid_reason.empty())
        invalid("/requirements", set.invalid_reason);

    const std::optional<ReceiverRequirementsSet> current =
        api.m_context.host.receiverRequirements();
    if (!current)
        return unavailable(api);
    // An edit that changes nothing opens no checkpoint and leaves the revision alone.
    if (*current == set)
        return requirementsResult(api.epoch(), api.m_context.commands.revision(),
                                  api.m_context.commands.isDirty(), set);

    api.m_context.host.beginCheckpoint(call);
    bool applied = false;
    try {
        applied = api.m_context.host.setReceiverRequirements(set);
    } catch (...) {
        // Applied-mode changes stay revertable through the committed snapshot.
        api.m_context.host.commitCheckpoint("interrupted: receiver requirements");
        throw;
    }
    if (!applied) {
        api.m_context.host.commitCheckpoint("interrupted: receiver requirements");
        return unavailable(api);
    }
    api.m_context.commands.markModified();
    api.m_context.host.commitCheckpoint("receiver requirements");
    return requirementsResult(api.epoch(), api.m_context.commands.revision(),
                              api.m_context.commands.isDirty(), set);
}
