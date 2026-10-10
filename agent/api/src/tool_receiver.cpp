#include "agent_args.h"
#include "agent_tools.h"
#include "receiver_performance_measurement.h"
#include "receiver_requirements.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
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

// A required object member of the arguments; errors address it as `/key`.
const OrderedJson &requiredObject(const OrderedJson &arguments, std::string_view key) {
    const std::string path = "/" + std::string(key);
    const auto found = arguments.find(std::string(key));
    if (found == arguments.end())
        invalid(path, "required field is missing");
    if (!found->is_object())
        invalid(path, "expected an object");
    return *found;
}

// A required finite number member of `object`; errors address it as `base/key`.
double requiredFinite(const OrderedJson &object, const std::string &base, std::string_view key) {
    const std::string path = base + "/" + std::string(key);
    const auto found = object.find(std::string(key));
    if (found == object.end())
        invalid(path, "required field is missing");
    if (!found->is_number())
        invalid(path, "expected a number");
    const double value = found->get<double>();
    if (!std::isfinite(value))
        invalid(path, "expected a finite number");
    return value;
}

// An optional finite number member of the arguments.
std::optional<double> optionalFinite(const OrderedJson &arguments, std::string_view key) {
    if (!arguments.contains(std::string(key)))
        return std::nullopt;
    return requiredFinite(arguments, "", key);
}

// The epoch every call carries. It is judged before any other argument, so a stale call reports
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

// The optional iip3 object: absent turns IIP3 off; present requires every field.
std::optional<ReceiverIIP3TestSettings> parseIip3(const OrderedJson &arguments) {
    const auto found = arguments.find("iip3");
    if (found == arguments.end())
        return std::nullopt;
    checkKeys(*found, "/iip3",
              {"tone_spacing_Hz", "input_start_dBm", "input_stop_dBm", "input_step_dB"});
    ReceiverIIP3TestSettings settings{};
    settings.tone_spacing_Hz = requiredFinite(*found, "/iip3", "tone_spacing_Hz");
    settings.input_start_dBm = requiredFinite(*found, "/iip3", "input_start_dBm");
    settings.input_stop_dBm = requiredFinite(*found, "/iip3", "input_stop_dBm");
    settings.input_step_dB = requiredFinite(*found, "/iip3", "input_step_dB");
    if (settings.tone_spacing_Hz <= 0.0 || settings.input_step_dB <= 0.0 ||
        settings.input_start_dBm > settings.input_stop_dBm || !receiverIIP3LevelCount(settings))
        invalid("/iip3", "tone spacing and input step must be positive, the start must not exceed "
                         "the stop, and the sweep may have at most " +
                             std::to_string(kMaxReceiverIIP3InputLevels) + " input levels");
    return settings;
}

// One entry per sweep point; NaN, which the engine uses for unavailable points, encodes as null.
Json encodeSeries(const std::vector<double> &values, std::size_t count) {
    Json series = Json::array();
    for (std::size_t index = 0; index < count; ++index)
        series.push_back(index < values.size() ? agentNumber(values[index]) : Json(nullptr));
    return series;
}

} // namespace

AgentToolResult executeReceiverMeasureTool(const AgentApi &api, const AgentCall &call) {
    const std::uint64_t epoch = api.epoch();
    if (requiredEpoch(call.arguments) != epoch)
        return staleMeasurementEpochError(epoch, api.m_last_replacement_cause,
                                          api.m_undone_summaries);
    AgentArgs args(call.arguments, {"epoch", "point_a", "point_b", "start_Hz", "stop_Hz", "points",
                                    "output_power", "reference_tone_Hz", "iip3"});

    const double start_Hz = requiredFinite(call.arguments, "", "start_Hz");
    const double stop_Hz = requiredFinite(call.arguments, "", "stop_Hz");
    if (start_Hz >= stop_Hz)
        invalid("/start_Hz", "start_Hz must be less than stop_Hz");
    const int points = args.optionalInt("points", 201, 2, 401);
    const bool output_power = args.optionalBool("output_power", true);
    const std::optional<double> reference_tone_Hz =
        optionalFinite(call.arguments, "reference_tone_Hz");
    const std::optional<ReceiverIIP3TestSettings> iip3 = parseIip3(call.arguments);

    const auto &context = api.m_context;
    const AgentEndpoint point_a =
        resolveOutputEndpoint(context, requiredObject(call.arguments, "point_a"), "/point_a");
    const AgentEndpoint point_b =
        resolveOutputEndpoint(context, requiredObject(call.arguments, "point_b"), "/point_b");
    const auto chain = findMeasurementChainPath(context.runtime.graph(), context.chain_host,
                                                point_a.pin, point_b.pin);
    if (!chain || chain->components.size() < 2) {
        AgentError error{AgentErrorCode::NoMeasurement,
                         "no measurement path exists between the selected ports"};
        error.details = {{"reason", "NO_PATH"}};
        return agentErrorResult(error, epoch);
    }

    std::vector<double> grid(static_cast<std::size_t>(points));
    for (std::size_t index = 0; index < grid.size(); ++index)
        grid[index] = start_Hz + (stop_Hz - start_Hz) * static_cast<double>(index) /
                                     static_cast<double>(points - 1);

    ReceiverMeasurementRequest request;
    request.output_power = output_power;
    request.reference_tone_Hz = reference_tone_Hz;
    request.iip3 = iip3.has_value();
    request.iip3_settings = iip3;
    ReceiverPerformanceMeasurementEngine &engine = *api.m_receiver_engine;
    engine.update(request, point_a.pin, point_b.pin, grid);

    const ReceiverPerformanceMeasurements &measurements = engine.measurements();
    AgentToolResult result;
    result.structured =
        Json{{"epoch", epoch},
             {"in_progress", engine.isInProgress()},
             {"frequencies_Hz", encodeSeries(grid, grid.size())},
             {"output_power_dBm", encodeSeries(measurements.output_power_dBm, grid.size())},
             {"iip3_dBm", encodeSeries(measurements.iip3_dBm, grid.size())}};
    return result;
}
