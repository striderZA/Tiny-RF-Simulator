#include "agent_args.h"
#include "agent_tools.h"
#include "receiver_requirements.h"
#include "rf_budget.h"
#include "rf_budget_cascade.h"
#include "rf_budget_stage.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

[[noreturn]] void fail(AgentError error) { throw AgentArgumentError(std::move(error)); }

[[noreturn]] void invalid(std::string path, std::string message) {
    AgentError error{AgentErrorCode::InvalidArgument, std::move(message)};
    error.details = {{"path", std::move(path)}};
    fail(std::move(error));
}

AgentToolResult internalError(const AgentApi &api, std::string message) {
    return agentErrorResult(AgentError{AgentErrorCode::Internal, std::move(message)}, api.epoch());
}

AgentToolResult notFound(const AgentApi &api, std::string message) {
    return agentErrorResult(AgentError{AgentErrorCode::NotFound, std::move(message)}, api.epoch());
}

AgentToolResult noMeasurement(const AgentApi &api, std::string reason, std::string message) {
    AgentError error{AgentErrorCode::NoMeasurement, std::move(message)};
    error.details = {{"reason", std::move(reason)}};
    return agentErrorResult(error, api.epoch());
}

// Unknown top-level argument keys, matching the schema's additionalProperties.
void checkArgumentKeys(const OrderedJson &arguments) {
    if (!arguments.is_object())
        invalid("/", "expected an object");
    for (const auto &member : arguments.items()) {
        const std::string_view key = member.key();
        if (key != "epoch" && key != "revision" && key != "budget")
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

// Walks every nested budget member, so a misspelled key is refused with its full path instead of
// being dropped. Params and per-stage overrides are free-form and stay unvisited.
void checkBudgetKeys(const OrderedJson &budget) {
    checkMemberKeys(budget, "/budget", {"format", "point_count", "stages", "outputs", "modes"});

    const auto stages = budget.find("stages");
    if (stages != budget.end() && stages->is_array()) {
        for (std::size_t index = 0; index < stages->size(); ++index) {
            const OrderedJson &stage = (*stages)[index];
            if (!stage.is_object())
                continue;
            const std::string path = "/budget/stages/" + std::to_string(index);
            checkMemberKeys(stage, path,
                            {"name", "type", "source", "part_number", "params", "inputs"});
            const auto inputs = stage.find("inputs");
            if (inputs == stage.end() || !inputs->is_array())
                continue;
            for (std::size_t input = 0; input < inputs->size(); ++input) {
                const OrderedJson &feed = (*inputs)[input];
                if (!feed.is_object())
                    continue;
                checkMemberKeys(feed, path + "/inputs/" + std::to_string(input),
                                {"from", "stage", "port"});
            }
        }
    }

    const auto outputs = budget.find("outputs");
    if (outputs != budget.end() && outputs->is_array()) {
        for (std::size_t index = 0; index < outputs->size(); ++index) {
            const OrderedJson &output = (*outputs)[index];
            if (!output.is_object())
                continue;
            checkMemberKeys(output, "/budget/outputs/" + std::to_string(index),
                            {"name", "stage", "port"});
        }
    }

    const auto modes = budget.find("modes");
    if (modes == budget.end() || !modes->is_array())
        return;
    for (std::size_t index = 0; index < modes->size(); ++index) {
        const OrderedJson &mode = (*modes)[index];
        if (!mode.is_object())
            continue;
        const std::string path = "/budget/modes/" + std::to_string(index);
        checkMemberKeys(mode, path, {"mode_id", "overrides"});
        const auto overrides = mode.find("overrides");
        if (overrides == mode.end() || !overrides->is_object())
            continue;
        // Stage names and their param keys are open: the model owns their validation.
    }
}

// The optional revision precondition from budget_get; absent means no precondition.
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

std::string_view requirementStatusName(ReceiverRequirementStatus status) {
    switch (status) {
    case ReceiverRequirementStatus::NotConfigured:
        return "not_configured";
    case ReceiverRequirementStatus::InvalidConfiguration:
        return "invalid_configuration";
    case ReceiverRequirementStatus::Incomplete:
        return "incomplete";
    case ReceiverRequirementStatus::Pass:
        return "pass";
    case ReceiverRequirementStatus::Fail:
        return "fail";
    }
    return "incomplete";
}

std::string_view modeStatusName(RfBudgetModeStatus status) {
    switch (status) {
    case RfBudgetModeStatus::Budgeted:
        return "budgeted";
    case RfBudgetModeStatus::NotBudgeted:
        return "not_budgeted";
    case RfBudgetModeStatus::NotConfigured:
        return "not_configured";
    }
    return "not_budgeted";
}

// Budget results keep ±infinity as a real value. JSON has no infinity literal, so the protocol
// writes them as strings; NaN still marks a point with no value. Other tools keep the shared
// agentNumber() contract, where every non-finite value reads as null.
Json budgetNumber(double value) {
    if (std::isnan(value))
        return Json(nullptr);
    if (std::isinf(value))
        return value > 0.0 ? Json("Infinity") : Json("-Infinity");
    return Json(value);
}

Json metricJson(const ReceiverMetricEvaluation &metric) {
    return {{"status", std::string(requirementStatusName(metric.status))},
            {"min", metric.observed_min ? budgetNumber(*metric.observed_min) : Json(nullptr)},
            {"max", metric.observed_max ? budgetNumber(*metric.observed_max) : Json(nullptr)}};
}

Json verdictJson(const ReceiverRequirementsEvaluation &verdict) {
    return {{"overall", std::string(requirementStatusName(verdict.overall))},
            {"gain", metricJson(verdict.gain)},
            {"noise_figure", metricJson(verdict.noise_figure)},
            {"output_power", metricJson(verdict.output_power)},
            {"iip3", metricJson(verdict.iip3)}};
}

Json reasonsJson(const RfBudgetReasonTable &reasons) {
    Json result = Json::array();
    for (const std::string &reason : reasons.all())
        result.push_back(reason);
    return result;
}

Json seriesJson(const RfBudgetSeries &series, std::size_t start, std::size_t count) {
    Json values = Json::array();
    Json reasons = Json::array();
    for (std::size_t index = start; index < start + count; ++index) {
        values.push_back(budgetNumber(series.values[index]));
        reasons.push_back(series.reasons[index]);
    }
    return {{"values", std::move(values)}, {"reasons", std::move(reasons)}};
}

Json stagesJson(const std::vector<RfBudgetStageResult> &stages, std::size_t start,
                std::size_t count) {
    Json result = Json::array();
    for (const RfBudgetStageResult &stage : stages) {
        result.push_back(Json{
            {"stage", stage.stage},
            {"input_port", stage.input_port},
            {"output_port", stage.output_port},
            {"gain_db", seriesJson(stage.gain_dB, start, count)},
            {"nf_db", seriesJson(stage.nf_dB, start, count)},
            {"oip3_dbm", seriesJson(stage.oip3_dBm, start, count)},
            {"cumulative_gain_db", seriesJson(stage.cumulative_gain_dB, start, count)},
            {"cumulative_nf_db", seriesJson(stage.cumulative_nf_dB, start, count)},
            {"cumulative_iip3_dbm", seriesJson(stage.cumulative_iip3_dBm, start, count)},
            {"level_dbm", seriesJson(stage.level_dBm, start, count)},
            {"nf_share_percent", seriesJson(stage.nf_share_percent, start, count)},
            {"p1db_headroom_db", seriesJson(stage.p1db_headroom_dB, start, count)},
        });
    }
    return result;
}

// The success shape shared by all three tools.
AgentToolResult budgetResult(std::uint64_t epoch, std::uint64_t revision, bool dirty,
                             const RfBudget &budget, const ReceiverRequirementsSet &requirements,
                             const ComponentLibrary &library) {
    const RfBudgetEvaluation evaluation = evaluateRfBudget(budget, requirements, library);

    Json modes = Json::array();
    for (const RfBudgetModeResult &mode : evaluation.modes) {
        Json outputs = Json::array();
        for (const RfBudgetCascade &cascade : mode.cascades)
            outputs.push_back(
                {{"output", cascade.output}, {"verdict", verdictJson(cascade.verdict)}});
        Json unreached = Json::array();
        for (const RfBudgetUnreachedOutput &entry : mode.unreached)
            unreached.push_back({{"output", entry.output}, {"reason", entry.reason}});
        modes.push_back({{"mode_id", mode.mode_id},
                         {"name", mode.mode_name},
                         {"status", std::string(modeStatusName(mode.status))},
                         {"outputs", std::move(outputs)},
                         {"unreached", std::move(unreached)}});
    }

    Json orphaned = Json::array();
    for (const std::uint32_t id : evaluation.orphaned_mode_ids)
        orphaned.push_back(id);

    AgentToolResult result;
    result.structured =
        Json{{"epoch", epoch},
             {"revision", revision},
             {"dirty", dirty},
             {"budget",
              budget.invalid_reason.empty() ? rfBudgetToJson(budget) : rfBudgetToJson(RfBudget{})},
             {"invalid_reason",
              budget.invalid_reason.empty() ? Json(nullptr) : Json(budget.invalid_reason)},
             {"not_evaluated_reason", evaluation.not_evaluated_reason.empty()
                                          ? Json(nullptr)
                                          : Json(evaluation.not_evaluated_reason)},
             {"modes", std::move(modes)},
             {"orphaned_mode_ids", std::move(orphaned)}};
    return result;
}

const RfBudgetModeResult *findMode(const RfBudgetEvaluation &evaluation, std::uint32_t mode_id) {
    for (const RfBudgetModeResult &mode : evaluation.modes) {
        if (mode.mode_id == mode_id)
            return &mode;
    }
    return nullptr;
}

const RfBudgetCascade *findCascade(const RfBudgetModeResult &mode, std::string_view output) {
    for (const RfBudgetCascade &cascade : mode.cascades) {
        if (cascade.output == output)
            return &cascade;
    }
    return nullptr;
}

const RfBudgetUnreachedOutput *findUnreached(const RfBudgetModeResult &mode,
                                             std::string_view output) {
    for (const RfBudgetUnreachedOutput &entry : mode.unreached) {
        if (entry.output == output)
            return &entry;
    }
    return nullptr;
}

bool hasOutput(const RfBudget &budget, std::string_view output) {
    return std::any_of(budget.outputs.begin(), budget.outputs.end(),
                       [&](const RfBudgetOutput &entry) { return entry.name == output; });
}

} // namespace

AgentToolResult executeBudgetGetTool(const AgentApi &api, const AgentCall &call) {
    // budget_get takes no arguments; AgentArgs rejects an object carrying any.
    AgentArgs args(call.arguments, {});
    const std::optional<RfBudget> budget = api.m_context.host.rfBudget();
    if (!budget)
        return internalError(api, "the RF budget is unavailable");
    const std::optional<ReceiverRequirementsSet> requirements =
        api.m_context.host.receiverRequirements();
    if (!requirements)
        return internalError(api, "receiver requirements are unavailable");
    return budgetResult(api.epoch(), api.m_context.commands.revision(),
                        api.m_context.commands.isDirty(), *budget, *requirements,
                        api.m_context.library);
}

AgentToolResult executeBudgetCascadeTool(const AgentApi &api, const AgentCall &call) {
    AgentArgs args(call.arguments, {"mode_id", "output", "start_index", "count"});
    const int mode_id = args.requiredInt("mode_id");
    const auto output_it = call.arguments.find("output");
    if (output_it == call.arguments.end())
        invalid("/output", "required field is missing");
    if (!output_it->is_string())
        invalid("/output", "expected a string");
    const std::string output = output_it->get<std::string>();
    const int start_index = args.optionalInt("start_index", 0, 0, std::numeric_limits<int>::max());
    const int count = args.optionalInt("count", 50, 1, 50);

    const std::optional<RfBudget> budget = api.m_context.host.rfBudget();
    if (!budget)
        return internalError(api, "the RF budget is unavailable");
    const std::optional<ReceiverRequirementsSet> requirements =
        api.m_context.host.receiverRequirements();
    if (!requirements)
        return internalError(api, "receiver requirements are unavailable");

    const std::uint32_t id = static_cast<std::uint32_t>(mode_id);
    const RfBudgetEvaluation evaluation =
        evaluateRfBudget(*budget, *requirements, api.m_context.library, id);
    if (!evaluation.not_evaluated_reason.empty())
        return noMeasurement(api, "NOT_EVALUATED", evaluation.not_evaluated_reason);
    const RfBudgetModeResult *mode = findMode(evaluation, id);
    if (!mode)
        return notFound(api,
                        "mode " + std::to_string(mode_id) + " is not in the receiver requirements");
    if (mode->status != RfBudgetModeStatus::Budgeted) {
        const std::string message =
            mode->status == RfBudgetModeStatus::NotBudgeted
                ? "mode '" + mode->mode_name + "' has no budget record"
                : "mode '" + mode->mode_name + "' has no receiver requirements";
        return noMeasurement(api,
                             mode->status == RfBudgetModeStatus::NotBudgeted ? "NOT_BUDGETED"
                                                                             : "NOT_CONFIGURED",
                             message);
    }

    const RfBudgetCascade *cascade = findCascade(*mode, output);
    if (!cascade) {
        if (!hasOutput(*budget, output))
            return notFound(api, "no output named '" + output + "'");
        const RfBudgetUnreachedOutput *unreached = findUnreached(*mode, output);
        return noMeasurement(api, "NOT_REACHED",
                             unreached ? unreached->reason
                                       : "output '" + output + "' is not reached in this mode");
    }

    const int point_count = static_cast<int>(cascade->frequencies_Hz.size());
    if (start_index >= point_count)
        invalid("/start_index", "expected a start index inside the measured points");
    const std::size_t begin = static_cast<std::size_t>(start_index);
    const std::size_t page = std::min<std::size_t>(static_cast<std::size_t>(count),
                                                   cascade->frequencies_Hz.size() - begin);

    Json frequencies = Json::array();
    for (std::size_t index = begin; index < begin + page; ++index)
        frequencies.push_back(agentNumber(cascade->frequencies_Hz[index]));

    AgentToolResult result;
    result.structured =
        Json{{"epoch", api.epoch()},
             {"mode_id", mode->mode_id},
             {"mode_name", mode->mode_name},
             {"output", cascade->output},
             {"point_count", point_count},
             {"start_index", start_index},
             {"next_index",
              begin + page < cascade->frequencies_Hz.size() ? Json(begin + page) : Json(nullptr)},
             {"frequencies_Hz", std::move(frequencies)},
             {"reasons", reasonsJson(evaluation.reasons)},
             {"stages", stagesJson(cascade->stages, begin, page)},
             {"verdict", verdictJson(cascade->verdict)}};
    return result;
}

AgentToolResult executeBudgetEditTool(const AgentApi &api, const AgentCall &call) {
    const std::uint64_t epoch = api.epoch();
    if (requiredEpoch(call.arguments) != epoch)
        return staleMeasurementEpochError(epoch, api.m_last_replacement_cause,
                                          api.m_undone_summaries);
    checkArgumentKeys(call.arguments);
    const std::optional<std::uint64_t> revision = optionalRevision(call.arguments);

    const auto found = call.arguments.find("budget");
    if (found == call.arguments.end())
        invalid("/budget", "required field is missing");
    if (!found->is_object())
        invalid("/budget", "expected an object");

    const auto format = found->find("format");
    if (format == found->end())
        invalid("/budget/format", "required field is missing");
    if ((!format->is_number_integer() && !format->is_number_unsigned()) ||
        format->get<long long>() != kRfBudgetFormat)
        invalid("/budget/format", "expected 1");
    checkBudgetKeys(*found);

    RfBudget decoded = rfBudgetFromJson(*found);
    if (!decoded.invalid_reason.empty())
        invalid("/budget", decoded.invalid_reason);
    // Decoding accepts a partial table so the view can build one in steps; the agent contract
    // still accepts only a complete budget.
    if (auto reason = validateRfBudget(decoded))
        invalid("/budget", *reason);

    const std::optional<RfBudget> current_budget = api.m_context.host.rfBudget();
    const std::optional<ReceiverRequirementsSet> requirements =
        api.m_context.host.receiverRequirements();
    // A record may keep an id already stored, so removing a requirements mode and re-adding it
    // later does not silently attach an unrelated budget; a new unknown id is refused.
    if (requirements) {
        for (std::size_t index = 0; index < decoded.modes.size(); ++index) {
            const std::uint32_t id = decoded.modes[index].mode_id;
            const bool current_mode =
                std::any_of(requirements->modes.begin(), requirements->modes.end(),
                            [&](const ReceiverRequirementsMode &mode) { return mode.id == id; });
            const bool stored = current_budget.has_value() &&
                                findRfBudgetModeRecord(*current_budget, id) != nullptr;
            if (!current_mode && !stored)
                invalid("/budget/modes/" + std::to_string(index) + "/mode_id",
                        "no requirements mode has id " + std::to_string(id));
        }
    }

    if (const auto issue = checkRfBudgetParams(decoded)) {
        AgentError error = agentParamError(issue->result, 0);
        error.op_index.reset();
        error.details["path"] = issue->path;
        return agentErrorResult(error, epoch);
    }

    if (!current_budget)
        return internalError(api, "the RF budget is unavailable");
    if (!requirements)
        return internalError(api, "receiver requirements are unavailable");
    // A revision from budget_get refuses an edit made against an older read, so a GUI Apply or
    // circuit edit since then survives.
    if (revision && *revision != api.m_context.commands.revision())
        return agentErrorResult(AgentError{AgentErrorCode::Busy,
                                           "the project changed since budget_get; read the RF "
                                           "budget again and retry"},
                                epoch);
    // An edit that changes nothing opens no checkpoint and leaves the revision alone.
    if (*current_budget == decoded)
        return budgetResult(api.epoch(), api.m_context.commands.revision(),
                            api.m_context.commands.isDirty(), decoded, *requirements,
                            api.m_context.library);

    // The reply must fit one protocol line. The check runs before the commit: a reply the server
    // cannot send becomes INTERNAL after the budget is stored. The revision and dirty flag are the
    // pre-commit values; the reserve in agentReplyFitsProtocolLine covers the difference.
    if (!agentReplyFitsProtocolLine(budgetResult(api.epoch(), api.m_context.commands.revision(),
                                                 api.m_context.commands.isDirty(), decoded,
                                                 *requirements, api.m_context.library)))
        invalid("/budget", "the budget is too large to read back in one reply; shorten its part "
                           "numbers or remove entries");

    api.m_context.host.beginCheckpoint(call);
    bool applied = false;
    try {
        applied = api.m_context.host.setRfBudget(decoded);
    } catch (...) {
        // Applied-mode changes stay revertable through the committed snapshot.
        api.m_context.host.commitCheckpoint("interrupted: rf budget");
        throw;
    }
    if (!applied) {
        api.m_context.host.discardCheckpoint();
        return internalError(api, "the host rejected the RF budget");
    }
    api.m_context.commands.markModified();
    api.m_context.host.commitCheckpoint("rf budget");
    return budgetResult(api.epoch(), api.m_context.commands.revision(),
                        api.m_context.commands.isDirty(), decoded, *requirements,
                        api.m_context.library);
}
