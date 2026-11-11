#include "agent_args.h"
#include "agent_tools.h"
#include "receiver_requirements.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
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

// Unknown top-level argument keys, matching the schema's additionalProperties.
void checkArgumentKeys(const OrderedJson &arguments) {
    if (!arguments.is_object())
        invalid("/", "expected an object");
    for (const auto &member : arguments.items()) {
        const std::string_view key = member.key();
        if (key != "epoch" && key != "requirements" && key != "revision")
            invalid("/" + std::string(key), "unknown field");
    }
}

// Rejects a member of `object` outside `allowed`, reported at its full JSON pointer.
void checkMemberKeys(const OrderedJson &object, const std::string &path,
                     std::initializer_list<std::string_view> allowed) {
    for (const auto &member : object.items()) {
        const std::string_view key = member.key();
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            invalid(path + "/" + std::string(key), "unknown field");
    }
}

// Checks a config object and its measurement conditions. Members of other types are left to the
// shared parser, which reports them.
void checkConfigKeys(const OrderedJson &config, const std::string &path) {
    checkMemberKeys(config, path,
                    {"band_start_hz", "band_stop_hz", "gain_min_db", "gain_max_db", "nf_max_db",
                     "output_power_min_dbm", "output_power_max_dbm", "iip3_min_dbm",
                     "measurement_conditions"});
    const auto conditions = config.find("measurement_conditions");
    if (conditions == config.end() || !conditions->is_object())
        return;
    const std::string conditions_path = path + "/measurement_conditions";
    checkMemberKeys(*conditions, conditions_path, {"input_level_dbm", "iip3"});
    const auto iip3 = conditions->find("iip3");
    if (iip3 != conditions->end() && iip3->is_object())
        checkMemberKeys(*iip3, conditions_path + "/iip3",
                        {"tone_spacing_hz", "input_start_dbm", "input_stop_dbm", "input_step_db"});
}

// Walks every nested member of the requirements object, so a misspelled key is refused with its
// full path instead of being dropped. The shared file parser stays lenient for legacy files.
void checkRequirementsKeys(const OrderedJson &requirements, bool with_ids) {
    checkMemberKeys(requirements, "/requirements", {"format", "selected_mode", "modes"});
    const auto modes = requirements.find("modes");
    if (modes == requirements.end() || !modes->is_array())
        return;
    for (std::size_t index = 0; index < modes->size(); ++index) {
        const OrderedJson &mode = (*modes)[index];
        if (!mode.is_object())
            continue;
        const std::string mode_path = "/requirements/modes/" + std::to_string(index);
        checkMemberKeys(mode, mode_path,
                        with_ids ? std::initializer_list<std::string_view>{"name", "config", "id"}
                                 : std::initializer_list<std::string_view>{"name", "config"});
        const auto config = mode.find("config");
        if (config != mode.end() && config->is_object())
            checkConfigKeys(*config, mode_path + "/config");
    }
}

// The optional revision precondition from requirements_get; absent means no precondition.
std::optional<std::uint64_t> optionalRevision(const OrderedJson &arguments) {
    const auto found = arguments.find("revision");
    if (found == arguments.end())
        return std::nullopt;
    if (found->is_number_unsigned())
        return found->get<std::uint64_t>();
    if (found->is_number_integer() && found->get<std::int64_t>() >= 0)
        return static_cast<std::uint64_t>(found->get<std::int64_t>());
    invalid("/revision", "expected a non-negative integer");
}

AgentToolResult unavailable(const AgentApi &api) {
    return agentErrorResult(
        AgentError{AgentErrorCode::Internal, "receiver requirements are unavailable"}, api.epoch());
}

// The success shape shared by both tools. A malformed retained set has no modes to encode, so the
// empty format-2 object is sent and the diagnostic travels in `invalid_reason`.
AgentToolResult requirementsResult(std::uint64_t epoch, std::uint64_t revision, bool dirty,
                                   const ReceiverRequirementsSet &set) {
    // Results omit next_mode_id: the host assigns it, and an edit never sends it.
    Json requirements =
        receiverRequirementsToJson(set.invalid_reason.empty() ? set : ReceiverRequirementsSet{});
    requirements.erase("next_mode_id");
    AgentToolResult result;
    result.structured = Json{
        {"epoch", epoch},
        {"revision", revision},
        {"dirty", dirty},
        {"requirements", std::move(requirements)},
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
    const std::optional<std::uint64_t> revision = optionalRevision(call.arguments);

    const auto found = call.arguments.find("requirements");
    if (found == call.arguments.end())
        invalid("/requirements", "required field is missing");
    if (!found->is_object())
        invalid("/requirements", "expected an object");

    const auto format = found->find("format");
    if (format == found->end())
        invalid("/requirements/format", "required field is missing");
    if (!format->is_number_integer() && !format->is_number_unsigned())
        invalid("/requirements/format", "expected 2 or 3");
    const long long format_number = format->get<long long>();
    if (format_number != 2 && format_number != kReceiverRequirementsFormat)
        invalid("/requirements/format", "expected 2 or 3");
    checkRequirementsKeys(*found, format_number == kReceiverRequirementsFormat);

    const std::optional<ReceiverRequirementsSet> current =
        api.m_context.host.receiverRequirements();
    if (!current)
        return unavailable(api);
    std::string error_path;
    std::string error;
    std::optional<ReceiverRequirementsSet> decoded =
        receiverRequirementsFromEdit(*found, *current, error_path, error);
    if (!decoded)
        invalid(error_path, error);
    ReceiverRequirementsSet set = std::move(*decoded);
    // A revision from requirements_get refuses an edit made against an older read, so a GUI Apply
    // or circuit edit since then survives. Mode selection does not advance the revision.
    if (revision && *revision != api.m_context.commands.revision())
        return agentErrorResult(AgentError{AgentErrorCode::Busy,
                                           "the project changed since requirements_get; read the "
                                           "requirements again and retry"},
                                epoch);
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
        // The host has requirements state but refused the write. Nothing was applied, so the open
        // checkpoint is discarded; only a throw commits it as interrupted.
        api.m_context.host.discardCheckpoint();
        return agentErrorResult(
            AgentError{AgentErrorCode::Internal, "the host rejected the receiver requirements"},
            epoch);
    }
    api.m_context.commands.markModified();
    api.m_context.host.commitCheckpoint("receiver requirements");
    return requirementsResult(api.epoch(), api.m_context.commands.revision(),
                              api.m_context.commands.isDirty(), set);
}
