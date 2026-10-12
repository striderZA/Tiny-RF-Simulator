#include "agent_args.h"
#include "agent_tools.h"
#include "flow_boundary.h"

#include "logging_core.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

constexpr std::size_t kMaxConditions = 4;
constexpr std::size_t kMaxMeasurements = 8;
constexpr std::uint64_t kMaxRows = 1000;
constexpr const char *kLatchHint = "reload the project before running flows again";

[[noreturn]] void fail(AgentError error) { throw AgentArgumentError(std::move(error)); }

[[noreturn]] void invalid(std::string path, std::string message) {
    AgentError error{AgentErrorCode::InvalidArgument, std::move(message)};
    error.details = {{"path", std::move(path)}};
    fail(std::move(error));
}

// Rejects any member of `object` outside `allowed`, matching the schema's additionalProperties.
void checkKeys(const OrderedJson &object, const std::string &path,
               std::initializer_list<std::string_view> allowed) {
    if (!object.is_object())
        invalid(path, "expected an object");
    for (const auto &member : object.items()) {
        const std::string_view key = member.key();
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            invalid(path + "/" + std::string(key), "unknown field");
    }
}

// A required member of `object`; errors address it as `base/key`.
const OrderedJson &requiredMember(const OrderedJson &object, const std::string &base,
                                  std::string_view key) {
    const auto found = object.find(std::string(key));
    if (found == object.end())
        invalid(base + "/" + std::string(key), "required field is missing");
    return *found;
}

int requiredIndex(const OrderedJson &object, const std::string &base, std::string_view key) {
    const OrderedJson &value = requiredMember(object, base, key);
    const std::string path = base + "/" + std::string(key);
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
            invalid(path, "integer is out of range");
        return static_cast<int>(number);
    }
    if (!value.is_number_integer())
        invalid(path, "expected a non-negative integer");
    const auto number = value.get<std::int64_t>();
    if (number < 0)
        invalid(path, "expected a non-negative integer");
    if (number > std::numeric_limits<int>::max())
        invalid(path, "integer is out of range");
    return static_cast<int>(number);
}

std::string requiredString(const OrderedJson &object, const std::string &base,
                           std::string_view key) {
    const OrderedJson &value = requiredMember(object, base, key);
    if (!value.is_string())
        invalid(base + "/" + std::string(key), "expected a string");
    return value.get<std::string>();
}

double finiteNumber(const OrderedJson &value, const std::string &path) {
    if (!value.is_number())
        invalid(path, "expected a number");
    const double number = value.get<double>();
    if (!std::isfinite(number))
        invalid(path, "expected a finite number");
    return number;
}

Condition parseCondition(const OrderedJson &item, std::size_t index) {
    const std::string base = "/conditions/" + std::to_string(index);
    checkKeys(item, base, {"component", "path", "values"});
    Condition condition;
    condition.component = requiredIndex(item, base, "component");
    condition.path = requiredString(item, base, "path");
    const OrderedJson &values = requiredMember(item, base, "values");
    const std::string values_path = base + "/values";
    if (!values.is_array())
        invalid(values_path, "expected an array");
    if (values.empty())
        invalid(values_path, "expected at least one value");
    for (std::size_t position = 0; position < values.size(); ++position)
        condition.values.push_back(
            finiteNumber(values[position], values_path + "/" + std::to_string(position)));
    return condition;
}

Measurement parseMeasurement(const OrderedJson &item, std::size_t index) {
    const std::string base = "/measure/" + std::to_string(index);
    checkKeys(item, base, {"component", "port", "metric"});
    Measurement measurement;
    measurement.component = requiredIndex(item, base, "component");
    measurement.port = requiredIndex(item, base, "port");
    measurement.metric = requiredString(item, base, "metric");
    return measurement;
}

// Rows the sweep produces: the product of the value counts, so no conditions give one row. The
// product saturates, so an absurd sweep is refused rather than wrapping around.
std::uint64_t rowCountOf(const std::vector<Condition> &conditions) {
    constexpr std::uint64_t kSaturated = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t rows = 1;
    for (const Condition &condition : conditions) {
        const auto count = static_cast<std::uint64_t>(condition.values.size());
        if (count != 0 && rows > kSaturated / count)
            return kSaturated;
        rows *= count;
    }
    return rows;
}

AgentErrorCode agentCodeFor(FlowErrorCode code) {
    switch (code) {
    case FlowErrorCode::ComponentNotFound:
    case FlowErrorCode::PortOutOfRange:
        return AgentErrorCode::NotFound;
    case FlowErrorCode::PathNotApplicable:
        return AgentErrorCode::ParamRejected;
    case FlowErrorCode::UnknownMetric:
    case FlowErrorCode::EmptyMeasurement:
    case FlowErrorCode::DuplicateConditionTarget:
    case FlowErrorCode::DuplicateMeasurement:
    case FlowErrorCode::FileUnreadable:
    case FlowErrorCode::InvalidJson:
    case FlowErrorCode::UnsupportedVersion:
    case FlowErrorCode::WrongShape:
    case FlowErrorCode::BadFieldType:
        return AgentErrorCode::InvalidArgument;
    case FlowErrorCode::None:
    case FlowErrorCode::CyclicGraph:
    case FlowErrorCode::DeserializeFailed:
    case FlowErrorCode::SerializeFailed:
        break;
    }
    return AgentErrorCode::Internal;
}

Json encodeRow(const FlowRow &row) {
    Json conditions = Json::array();
    for (const ConditionValue &swept : row.conditions)
        conditions.push_back(
            {{"component", swept.component}, {"path", swept.path}, {"value", swept.value}});
    Json metrics = Json::array();
    for (const MetricSample &sample : row.metrics)
        metrics.push_back({{"component", sample.component},
                           {"port", sample.port},
                           {"name", sample.name},
                           {"value", agentNumber(sample.value)},
                           {"unit", sample.unit},
                           {"valid", sample.valid}});
    return {{"conditions", std::move(conditions)}, {"metrics", std::move(metrics)}};
}

Json encodeRows(const FlowResult &result) {
    Json rows = Json::array();
    for (const FlowRow &row : result.rows)
        rows.push_back(encodeRow(row));
    return rows;
}

std::optional<AgentToolResult>
parseTestFlowRequest(const AgentCall &call, std::uint64_t epoch,
                     const std::string &last_replacement_cause,
                     const std::vector<std::string> &undone_summaries,
                     const std::string &flow_latch_message, FlowSpec &spec) {
    AgentArgs args(call.arguments, {"epoch", "conditions", "measure"});
    const std::uint64_t sent_epoch = args.requiredUInt64("epoch");
    if (sent_epoch != epoch)
        return staleMeasurementEpochError(epoch, last_replacement_cause, undone_summaries);
    if (!flow_latch_message.empty()) {
        AgentError latched{AgentErrorCode::Internal, flow_latch_message, std::string(kLatchHint)};
        return agentErrorResult(latched, epoch);
    }

    std::vector<Condition> conditions;
    if (call.arguments.contains("conditions")) {
        const OrderedJson &list = call.arguments.at("conditions");
        if (!list.is_array())
            invalid("/conditions", "expected an array");
        if (list.size() > kMaxConditions)
            invalid("/conditions", "give at most 4 conditions");
        for (std::size_t index = 0; index < list.size(); ++index)
            conditions.push_back(parseCondition(list[index], index));
    }
    const OrderedJson &measure_list = requiredMember(call.arguments, "", "measure");
    if (!measure_list.is_array())
        invalid("/measure", "expected an array");
    if (measure_list.empty() || measure_list.size() > kMaxMeasurements)
        invalid("/measure", "give 1 to 8 measurements");
    std::vector<Measurement> measurements;
    for (std::size_t index = 0; index < measure_list.size(); ++index)
        measurements.push_back(parseMeasurement(measure_list[index], index));

    for (std::size_t later = 1; later < conditions.size(); ++later)
        for (std::size_t earlier = 0; earlier < later; ++earlier)
            if (conditions[earlier].component == conditions[later].component &&
                conditions[earlier].path == conditions[later].path)
                invalid("/conditions/" + std::to_string(later),
                        "two conditions sweep the same component and path");
    for (std::size_t later = 1; later < measurements.size(); ++later)
        for (std::size_t earlier = 0; earlier < later; ++earlier)
            if (measurements[earlier].component == measurements[later].component &&
                measurements[earlier].port == measurements[later].port &&
                measurements[earlier].metric == measurements[later].metric)
                invalid("/measure/" + std::to_string(later),
                        "two measurements read the same metric on the same port");

    const std::uint64_t rows = rowCountOf(conditions);
    if (rows > kMaxRows) {
        AgentError error{AgentErrorCode::InvalidArgument, "the sweep has " + std::to_string(rows) +
                                                              " rows; the limit is " +
                                                              std::to_string(kMaxRows)};
        error.details = {{"path", "/conditions"}, {"requested", rows}, {"limit", kMaxRows}};
        fail(std::move(error));
    }

    spec = FlowSpec{1, "agent", std::move(conditions), std::move(measurements)};
    return std::nullopt;
}

class CompletedFlowTask final : public IAgentCallTask {
  public:
    explicit CompletedFlowTask(AgentToolResult result) : m_result(std::move(result)) {}

    std::optional<AgentToolResult> advance(std::chrono::steady_clock::duration) override {
        if (!m_result)
            return std::nullopt;
        std::optional<AgentToolResult> result{std::move(*m_result)};
        m_result.reset();
        return result;
    }

    void cancel() noexcept override { m_result.reset(); }

  private:
    std::optional<AgentToolResult> m_result;
};
} // namespace

class AgentTestFlowRunTask final : public IAgentCallTask {
  public:
    AgentTestFlowRunTask(AgentApi &api, FlowSpec spec, std::uint64_t epoch)
        : m_api(api), m_epoch(epoch), m_revision(api.m_context.commands.revision()),
          m_session(spec, api.m_context.runtime.components().all(), api.m_context.runtime.graph()),
          m_encoded_rows(Json::array()) {}

    ~AgentTestFlowRunTask() override { cancel(); }

    std::optional<AgentToolResult> advance(std::chrono::steady_clock::duration budget) override {
        if (m_complete)
            return std::nullopt;

        const std::uint64_t current_epoch = m_api.epoch();
        if (current_epoch != m_epoch) {
            m_session.abandon();
            m_complete = true;
            return staleMeasurementEpochError(current_epoch, m_api.m_last_replacement_cause,
                                              m_api.m_undone_summaries);
        }
        if (m_api.m_context.commands.revision() != m_revision) {
            m_session.abandon();
            m_complete = true;
            return agentErrorResult(
                AgentError{
                    AgentErrorCode::Busy,
                    "the circuit changed during the flow run; retry with the current circuit"},
                current_epoch);
        }

        const bool complete = m_session.advance(kMaxRowsPerAdvance, budget);
        appendNewRows();
        if (!complete)
            return std::nullopt;

        AgentToolResult result =
            resultFor(m_api, m_session.outcome(), m_epoch, std::move(m_encoded_rows));
        m_complete = true;
        return result;
    }

    void cancel() noexcept override {
        if (m_complete)
            return;
        try {
            if (m_api.epoch() != m_epoch || m_api.m_context.commands.revision() != m_revision) {
                m_session.abandon();
            } else {
                m_session.cancel();
                if (m_session.outcome().status == BoundaryStatus::RestoreFailed)
                    static_cast<void>(resultFor(m_api, m_session.outcome(), m_epoch));
            }
        } catch (const std::exception &error) {
            LOG_ERROR("Agent test-flow cancellation failed: %s", error.what());
        } catch (...) {
            LOG_ERROR("Agent test-flow cancellation failed: %s", "unknown exception");
        }
        m_complete = true;
    }

    static AgentToolResult resultFor(AgentApi &api, const BoundaryOutcome &outcome,
                                     std::uint64_t epoch,
                                     std::optional<Json> encoded_rows = std::nullopt) {
        switch (outcome.status) {
        case BoundaryStatus::SnapshotFailed:
            return agentErrorResult(AgentError{AgentErrorCode::Internal,
                                               "cannot snapshot the circuit: " + outcome.message},
                                    epoch);
        case BoundaryStatus::ExecutionFailed:
            return agentErrorResult(
                AgentError{AgentErrorCode::Internal, "flow execution failed: " + outcome.message},
                epoch);
        case BoundaryStatus::RestoreFailed:
            api.m_flow_latch_message =
                "circuit could not be fully restored after a flow run: " + outcome.message;
            api.m_context.commands.markModified();
            return agentErrorResult(AgentError{AgentErrorCode::Internal, api.m_flow_latch_message,
                                               std::string(kLatchHint)},
                                    epoch);
        case BoundaryStatus::Completed:
            break;
        }

        const FlowResult &result = outcome.result;
        if (!result.ok)
            return agentErrorResult(
                AgentError{agentCodeFor(result.error.code), result.error.message}, epoch);

        AgentToolResult reply;
        Json rows = encoded_rows ? std::move(*encoded_rows) : encodeRows(result);
        reply.structured =
            Json{{"epoch", epoch}, {"row_count", result.rows.size()}, {"rows", std::move(rows)}};
        return reply;
    }

  private:
    static constexpr std::size_t kMaxRowsPerAdvance = 64;

    void appendNewRows() {
        const auto &rows = m_session.outcome().result.rows;
        for (; m_encoded_count < rows.size(); ++m_encoded_count)
            m_encoded_rows.push_back(encodeRow(rows[m_encoded_count]));
    }

    AgentApi &m_api;
    std::uint64_t m_epoch;
    std::uint64_t m_revision;
    FlowRunSession m_session;
    Json m_encoded_rows;
    std::size_t m_encoded_count = 0;
    bool m_complete = false;
};

AgentToolResult executeTestFlowRunTool(AgentApi &api, const AgentCall &call) {
    const std::uint64_t epoch = api.epoch();
    FlowSpec spec;
    if (auto early = parseTestFlowRequest(call, epoch, api.m_last_replacement_cause,
                                          api.m_undone_summaries, api.m_flow_latch_message, spec))
        return std::move(*early);
    const BoundaryOutcome outcome = api.m_run_boundary(
        spec, api.m_context.runtime.components().all(), api.m_context.runtime.graph());
    return AgentTestFlowRunTask::resultFor(api, outcome, epoch);
}

std::unique_ptr<IAgentCallTask> startTestFlowRunTask(AgentApi &api, const AgentCall &call) {
    const std::uint64_t epoch = api.epoch();
    FlowSpec spec;
    if (auto early = parseTestFlowRequest(call, epoch, api.m_last_replacement_cause,
                                          api.m_undone_summaries, api.m_flow_latch_message, spec))
        return std::make_unique<CompletedFlowTask>(std::move(*early));
    return std::make_unique<AgentTestFlowRunTask>(api, std::move(spec), epoch);
}
