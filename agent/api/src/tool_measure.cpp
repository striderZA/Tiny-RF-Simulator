#include "agent_tools.h"

#include "agent_args.h"
#include "flow_metrics.h"
#include "output_snr.h"
#include "power_meter_engine.h"
#include "tool_metadata.h"

#include <algorithm>
#include <cmath>
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

[[noreturn]] void invalid(std::string path, std::string message) {
    AgentError error{AgentErrorCode::InvalidArgument, std::move(message)};
    error.details = {{"path", std::move(path)}};
    throw AgentArgumentError(std::move(error));
}

[[noreturn]] void notFound(std::string message) {
    AgentError error{AgentErrorCode::NotFound, std::move(message)};
    throw AgentArgumentError(std::move(error));
}

const OrderedJson &requiredObject(const OrderedJson &object, std::string_view key,
                                  std::string_view path) {
    const auto it = object.find(std::string(key));
    if (it == object.end())
        invalid(std::string(path), "required field is missing");
    if (!it->is_object())
        invalid(std::string(path), "expected an object");
    return *it;
}

void checkKeys(const OrderedJson &object, std::initializer_list<std::string_view> keys,
               std::string_view path) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (std::find(keys.begin(), keys.end(), it.key()) == keys.end())
            invalid(std::string(path) + "/" + it.key(), "unknown field");
    }
}

double requiredFiniteNumber(const OrderedJson &object, std::string_view key,
                            std::string_view path) {
    const auto it = object.find(std::string(key));
    if (it == object.end())
        invalid(std::string(path), "required field is missing");
    if (!it->is_number())
        invalid(std::string(path), "expected a number");
    const double value = it->get<double>();
    if (!std::isfinite(value))
        invalid(std::string(path), "expected a finite number");
    return value;
}

int optionalBoundedInt(const OrderedJson &object, std::string_view key, int fallback, int minimum,
                       int maximum, std::string_view path) {
    const auto it = object.find(std::string(key));
    if (it == object.end())
        return fallback;
    if (!it->is_number_integer() && !it->is_number_unsigned())
        invalid(std::string(path), "expected an integer");
    if (it->is_number_unsigned()) {
        const auto value = it->get<std::uint64_t>();
        if (value < static_cast<std::uint64_t>(minimum) ||
            value > static_cast<std::uint64_t>(maximum))
            invalid(std::string(path), "integer is out of range");
        return static_cast<int>(value);
    }
    const auto value = it->get<std::int64_t>();
    if (value < minimum || value > maximum)
        invalid(std::string(path), "integer is out of range");
    return static_cast<int>(value);
}

std::optional<int> analyzerPoints(const OrderedJson &object) {
    const auto it = object.find("points");
    if (it == object.end())
        return std::nullopt;
    if (!it->is_number_integer() && !it->is_number_unsigned())
        invalid("/points", "expected an integer");
    if (it->is_number_unsigned()) {
        const auto value = it->get<std::uint64_t>();
        return value > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
                   ? std::numeric_limits<int>::max()
                   : static_cast<int>(value);
    }
    const auto value = it->get<std::int64_t>();
    return static_cast<int>(std::clamp<std::int64_t>(value, std::numeric_limits<int>::min(),
                                                     std::numeric_limits<int>::max()));
}

Json optionalNumber(double value) { return agentNumber(value); }

Json powerMeasurement(const Spectrum &spectrum, std::string_view name) {
    const auto *metric = MetricRegistry::instance().find(std::string(name));
    return metric ? optionalNumber(metric->compute(spectrum)) : Json(nullptr);
}

Json errorNoMeasurement(const AgentApi &api, std::string reason, std::string message) {
    AgentError error{AgentErrorCode::NoMeasurement, std::move(message)};
    error.details = {{"reason", std::move(reason)}};
    return agentErrorResult(error, api.epoch()).structured;
}

AgentToolResult errorResult(Json value) {
    AgentToolResult result;
    result.is_error = true;
    result.structured = std::move(value);
    return result;
}

} // namespace

AgentEndpoint resolveOutputEndpoint(const AgentApiContext &context, const OrderedJson &value,
                                    std::string_view path) {
    checkKeys(value, {"component", "port"}, path);
    const int component_id =
        optionalBoundedInt(value, "component", -1, 0, std::numeric_limits<int>::max(),
                           std::string(path) + "/component");
    const int port = optionalBoundedInt(value, "port", -1, 0, std::numeric_limits<int>::max(),
                                        std::string(path) + "/port");
    if (component_id < 0)
        invalid(std::string(path) + "/component", "required field is missing");
    if (port < 0)
        invalid(std::string(path) + "/port", "required field is missing");
    IComponentEngine *component = nullptr;
    for (auto *candidate : context.runtime.components().all()) {
        if (candidate->id() == component_id) {
            component = candidate;
            break;
        }
    }
    if (!component)
        notFound("component " + std::to_string(component_id) + " does not exist");
    if (port >= component->numOutputPins())
        notFound("output port " + std::to_string(port) + " does not exist on component " +
                 std::to_string(component_id));
    const int pin = component->outputPinId(port);
    if (pin < 0)
        notFound("output port " + std::to_string(port) + " does not exist on component " +
                 std::to_string(component_id));
    return {component, port, pin};
}

AgentToolResult staleMeasurementEpochError(std::uint64_t epoch, const std::string &cause,
                                           const std::vector<std::string> &undone) {
    AgentError error{AgentErrorCode::StaleEpoch, "epoch is stale; call circuit_get"};
    error.details = {{"epoch", epoch}};
    if (!cause.empty()) {
        error.details["cause"] = cause;
        if (cause == "reverted")
            error.details["undone"] = undone;
    }
    return agentErrorResult(error, epoch);
}

namespace {

std::vector<double> finiteValues(const std::vector<double> &values) {
    std::vector<double> valid;
    for (double value : values) {
        if (std::isfinite(value))
            valid.push_back(value);
    }
    return valid;
}

Json summary(const std::vector<double> &frequencies, const std::vector<double> &values,
             double center_frequency) {
    Json result{{"min", nullptr},      {"max", nullptr},       {"mean", nullptr},
                {"at_start", nullptr}, {"at_center", nullptr}, {"at_stop", nullptr}};
    if (frequencies.empty() || frequencies.size() != values.size())
        return result;
    auto valid = finiteValues(values);
    if (valid.empty())
        return result;
    const auto [minimum, maximum] = std::minmax_element(valid.begin(), valid.end());
    double mean = 0.0;
    for (double value : valid)
        mean += value / static_cast<double>(valid.size());
    result["min"] = agentNumber(*minimum);
    result["max"] = agentNumber(*maximum);
    result["mean"] = agentNumber(mean);
    const auto nearest = [&](double frequency) {
        auto index = static_cast<std::size_t>(
            std::min_element(frequencies.begin(), frequencies.end(),
                             [frequency](double left, double right) {
                                 return std::abs(left - frequency) < std::abs(right - frequency);
                             }) -
            frequencies.begin());
        return index;
    };
    result["at_start"] = agentNumber(values.front());
    result["at_center"] = agentNumber(values[nearest(center_frequency)]);
    result["at_stop"] = agentNumber(values.back());
    return result;
}
} // namespace

AgentToolResult executeMeasurePortTool(const AgentApi &api, const AgentCall &call) {
    const auto &context = api.m_context;
    AgentArgs args(call.arguments, {"epoch", "at", "max_tones", "trace"});
    const auto sent_epoch = args.requiredUInt64("epoch");
    const auto epoch = api.epoch();
    if (sent_epoch != epoch) {
        return staleMeasurementEpochError(epoch, api.m_last_replacement_cause,
                                          api.m_undone_summaries);
    }
    const auto &at_json = requiredObject(call.arguments, "at", "/at");
    const AgentEndpoint at = resolveOutputEndpoint(context, at_json, "/at");

    // Recompute synchronously so parameter edits are visible even before the next UI/DSP frame.
    context.runtime.update(0.0);
    const auto &outputs = at.component->node().outputs;
    if (static_cast<std::size_t>(at.port) >= outputs.size())
        return errorResult(errorNoMeasurement(api, "NO_VALID_POINTS", "output has no measurement"));
    const Spectrum &spectrum = outputs[static_cast<std::size_t>(at.port)];
    const int max_tones = optionalBoundedInt(call.arguments, "max_tones", 8, 1, 32, "/max_tones");
    Json tones = Json::array();
    std::vector<Spectrum::Tone> sorted_tones = spectrum.tones;
    std::stable_sort(sorted_tones.begin(), sorted_tones.end(),
                     [](const auto &left, const auto &right) {
                         const bool left_finite = std::isfinite(left.power_dBm);
                         const bool right_finite = std::isfinite(right.power_dBm);
                         if (left_finite != right_finite)
                             return left_finite;
                         // Keep non-finite powers in one stable tail class.
                         if (!left_finite)
                             return false;
                         return left.power_dBm > right.power_dBm;
                     });
    const std::size_t tone_count = sorted_tones.size();
    if (sorted_tones.size() > static_cast<std::size_t>(max_tones))
        sorted_tones.resize(static_cast<std::size_t>(max_tones));
    for (const auto &tone : sorted_tones)
        tones.push_back({{"freq_Hz", agentNumber(tone.freq_Hz)},
                         {"power_dBm", agentNumber(tone.power_dBm)},
                         {"phase_deg", agentNumber(tone.phase_deg)}});

    const OutputSnr snr = computeOutputSnr(*at.component, at.port, context.spectrum_analyzer);
    Json snr_basis;
    if (snr.basis == SnrBasis::PfbChannel) {
        snr_basis = {{"kind", "pfb_channel"},
                     {"enbw_Hz", agentNumber(snr.enbw_Hz)},
                     {"channel_noise_dBm",
                      snr.channel_noise_dBm ? agentNumber(*snr.channel_noise_dBm) : Json(nullptr)}};
    } else {
        snr_basis = {{"kind", "rbw"}, {"rbw_Hz", agentNumber(snr.rbw_Hz)}};
    }
    Json result{{"epoch", epoch},
                {"total_power_dBm", powerMeasurement(spectrum, "power_dBm")},
                {"peak",
                 {{"freq_Hz", powerMeasurement(spectrum, "peak_freq_Hz")},
                  {"power_dBm", powerMeasurement(spectrum, "peak_power_dBm")}}},
                {"noise_floor_dBm_per_Hz", powerMeasurement(spectrum, "noise_floor_dBm_per_Hz")},
                {"tones", std::move(tones)},
                {"tone_count", tone_count},
                {"snr_dB", snr.snr_dB ? agentNumber(*snr.snr_dB) : Json(nullptr)},
                {"snr_basis", std::move(snr_basis)},
                {"fs_Hz", agentNumber(spectrum.fs_Hz)},
                {"is_complex_baseband", spectrum.is_complex_baseband}};

    const auto trace_it = call.arguments.find("trace");
    if (trace_it != call.arguments.end()) {
        if (!trace_it->is_object())
            invalid("/trace", "expected an object");
        checkKeys(*trace_it, {"start_Hz", "stop_Hz", "points"}, "/trace");
        const double start = requiredFiniteNumber(*trace_it, "start_Hz", "/trace/start_Hz");
        const double stop = requiredFiniteNumber(*trace_it, "stop_Hz", "/trace/stop_Hz");
        if (!(start < stop))
            invalid("/trace", "start_Hz must be less than stop_Hz");
        const int points = optionalBoundedInt(*trace_it, "points", 201, 2, 401, "/trace/points");
        Json trace_frequencies = Json::array();
        Json trace_noise = Json::array();
        const double divisor = static_cast<double>(points);
        for (int index = 0; index < points; ++index) {
            const double low = std::lerp(start, stop, static_cast<double>(index) / divisor);
            const double high = std::lerp(start, stop, static_cast<double>(index + 1) / divisor);
            const double center =
                std::lerp(start, stop, (static_cast<double>(index) + 0.5) / divisor);
            double sum = 0.0;
            std::size_t count = 0;
            const std::size_t bins =
                std::min(spectrum.frequencies.size(), spectrum.noise_total_W.size());
            for (std::size_t bin = 0; bin < bins; ++bin) {
                if (spectrum.frequencies[bin] >= low &&
                    (index == points - 1 ? spectrum.frequencies[bin] <= high
                                         : spectrum.frequencies[bin] < high) &&
                    std::isfinite(spectrum.noise_total_W[bin]) &&
                    spectrum.noise_total_W[bin] >= 0.0) {
                    sum += spectrum.noise_total_W[bin];
                    ++count;
                }
            }
            double density = std::numeric_limits<double>::quiet_NaN();
            if (count != 0) {
                density = sum / static_cast<double>(count);
            } else if (bins != 0) {
                std::size_t nearest = 0;
                for (std::size_t bin = 1; bin < bins; ++bin) {
                    if (std::abs(spectrum.frequencies[bin] - center) <
                        std::abs(spectrum.frequencies[nearest] - center))
                        nearest = bin;
                }
                density = spectrum.noise_total_W[nearest];
            }
            const double density_dBm = density > 0.0 ? 10.0 * std::log10(density) + 30.0
                                                     : std::numeric_limits<double>::quiet_NaN();
            trace_frequencies.push_back(agentNumber(center));
            trace_noise.push_back(agentNumber(density_dBm));
        }
        result["trace"] = {{"frequencies_Hz", std::move(trace_frequencies)},
                           {"noise_dBm_per_Hz", std::move(trace_noise)}};
    }
    AgentToolResult tool_result;
    tool_result.structured = std::move(result);
    return tool_result;
}

AgentToolResult executeNetworkAnalyzerSweepTool(const AgentApi &api, const AgentCall &call) {
    auto &engine = api.m_context.network_analyzer;
    const auto &context = api.m_context;
    AgentArgs args(call.arguments, {"epoch", "point_a", "point_b", "start_Hz", "stop_Hz", "points",
                                    "stimulus_dBm", "arrays"});
    const auto sent_epoch = args.requiredUInt64("epoch");
    const auto epoch = api.epoch();
    if (sent_epoch != epoch) {
        return staleMeasurementEpochError(epoch, api.m_last_replacement_cause,
                                          api.m_undone_summaries);
    }
    const AgentEndpoint point_a = resolveOutputEndpoint(
        context, requiredObject(call.arguments, "point_a", "/point_a"), "/point_a");
    const AgentEndpoint point_b = resolveOutputEndpoint(
        context, requiredObject(call.arguments, "point_b", "/point_b"), "/point_b");
    std::optional<double> start;
    std::optional<double> stop;
    std::optional<double> stimulus;
    const auto start_it = call.arguments.find("start_Hz");
    if (start_it != call.arguments.end()) {
        if (!start_it->is_number() || !std::isfinite(start_it->get<double>()))
            invalid("/start_Hz", "expected a finite number");
        start = start_it->get<double>();
    }
    const auto stop_it = call.arguments.find("stop_Hz");
    if (stop_it != call.arguments.end()) {
        if (!stop_it->is_number() || !std::isfinite(stop_it->get<double>()))
            invalid("/stop_Hz", "expected a finite number");
        stop = stop_it->get<double>();
    }
    const auto points = analyzerPoints(call.arguments);
    const auto stimulus_it = call.arguments.find("stimulus_dBm");
    if (stimulus_it != call.arguments.end()) {
        if (!stimulus_it->is_number() || !std::isfinite(stimulus_it->get<double>()))
            invalid("/stimulus_dBm", "expected a finite number");
        stimulus = stimulus_it->get<double>();
    }
    int max_points = 0;
    const auto arrays_it = call.arguments.find("arrays");
    if (arrays_it != call.arguments.end()) {
        if (!arrays_it->is_object())
            invalid("/arrays", "expected an object");
        checkKeys(*arrays_it, {"max_points"}, "/arrays");
        max_points = optionalBoundedInt(*arrays_it, "max_points", 0, 2, 401, "/arrays/max_points");
        if (max_points == 0)
            invalid("/arrays/max_points", "required field is missing");
    }
    if ((start && !stop && *start >= engine.stopFrequency()) ||
        (!start && stop && engine.startFrequency() >= *stop) || (start && stop && *start >= *stop))
        invalid("/stop_Hz", "start_Hz must be less than stop_Hz");

    const double next_start = start.value_or(engine.startFrequency());
    const double next_stop = stop.value_or(engine.stopFrequency());
    const int next_points = std::clamp(points.value_or(engine.points()), 2, 2001);
    const double next_stimulus = stimulus.value_or(engine.stimulusPower());
    const bool changed = next_start != engine.startFrequency() ||
                         next_stop != engine.stopFrequency() || next_points != engine.points() ||
                         next_stimulus != engine.stimulusPower() ||
                         point_a.pin != engine.pointAPin() || point_b.pin != engine.pointBPin();
    if (changed)
        context.host.beginCheckpoint(call);
    engine.setPointA(point_a.pin);
    engine.setPointB(point_b.pin);
    if (start)
        engine.setStartFrequency(*start);
    if (stop)
        engine.setStopFrequency(*stop);
    if (points)
        engine.setPoints(*points);
    if (stimulus)
        engine.setStimulusPower(*stimulus);
    if (changed) {
        context.commands.markModified();
        context.host.commitCheckpoint("network analyzer settings");
    }
    engine.update();

    const auto &frequencies = engine.sweepFrequencies();
    const auto &gain = engine.gainDb();
    const auto &nf = engine.noiseFigureDb();
    std::size_t valid_points = 0;
    for (std::size_t i = 0; i < frequencies.size() && i < gain.size() && i < nf.size(); ++i)
        if (std::isfinite(gain[i]) && std::isfinite(nf[i]))
            ++valid_points;
    if (valid_points == 0) {
        const auto path = findMeasurementChainPath(context.runtime.graph(), context.chain_host,
                                                   point_a.pin, point_b.pin);
        const std::string reason =
            !path || path->components.size() < 2 ? "NO_PATH" : "NO_VALID_POINTS";
        return errorResult(errorNoMeasurement(
            api, reason,
            reason == "NO_PATH" ? "no measurement path exists between the selected ports"
                                : "the measurement path has no valid sweep points"));
    }
    Json gain_summary =
        summary(frequencies, gain, (engine.startFrequency() + engine.stopFrequency()) * 0.5);
    Json nf_summary =
        summary(frequencies, nf, (engine.startFrequency() + engine.stopFrequency()) * 0.5);
    Json settings{{"point_a", {{"component", point_a.component->id()}, {"port", point_a.port}}},
                  {"point_b", {{"component", point_b.component->id()}, {"port", point_b.port}}},
                  {"start_Hz", agentNumber(engine.startFrequency())},
                  {"stop_Hz", agentNumber(engine.stopFrequency())},
                  {"points", engine.points()},
                  {"stimulus_dBm", agentNumber(engine.stimulusPower())}};
    Json summary_json{{"valid_points", valid_points},
                      {"gain_dB", std::move(gain_summary)},
                      {"nf_dB", std::move(nf_summary)}};
    Json result{
        {"epoch", epoch}, {"settings", std::move(settings)}, {"summary", std::move(summary_json)}};
    if (max_points > 0) {
        Json out_frequencies = Json::array();
        Json out_gain = Json::array();
        Json out_nf = Json::array();
        const std::size_t count = frequencies.size();
        const std::size_t samples =
            std::min<std::size_t>(static_cast<std::size_t>(max_points), count);
        for (std::size_t k = 0; k < samples; ++k) {
            const std::size_t index =
                samples == 1 ? 0
                             : static_cast<std::size_t>(std::llround(
                                   static_cast<double>(k) * static_cast<double>(count - 1) /
                                   static_cast<double>(samples - 1)));
            out_frequencies.push_back(agentNumber(frequencies[index]));
            out_gain.push_back(agentNumber(gain[index]));
            out_nf.push_back(agentNumber(nf[index]));
        }
        result["arrays"] = {{"frequencies_Hz", std::move(out_frequencies)},
                            {"gain_dB", std::move(out_gain)},
                            {"nf_dB", std::move(out_nf)}};
    }
    AgentToolResult tool_result;
    tool_result.structured = std::move(result);
    return tool_result;
}
