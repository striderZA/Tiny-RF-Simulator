#include "flow_runner.h"

#include "component_interface.h"
#include "flow_metrics.h"
#include "flow_params.h"
#include "node_graph_engine.h"
#include "rewire.h"
#include "signal_node.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

FlowResult fail(const FlowSpec &spec, FlowErrorCode code, const std::string &message) {
    FlowResult result;
    result.name = spec.name;
    result.ok = false;
    result.error.code = code;
    result.error.message = message;
    return result;
}

IComponentEngine *engineById(std::span<IComponentEngine *const> components, int id) {
    for (auto *component : components) {
        if (component && component->id() == id)
            return component;
    }
    return nullptr;
}

std::string knownMetricNames() {
    std::string out;
    for (const auto &name : MetricRegistry::instance().names()) {
        if (!out.empty())
            out += ", ";
        out += name;
    }
    return out;
}

} // namespace

FlowLoadResult LoadFlowFile(const std::string &path) {
    FlowLoadResult out;
    const auto fail = [&](FlowErrorCode code, const std::string &message) {
        out.ok = false;
        out.error.code = code;
        out.error.message = message;
        // A failure must not leak partially parsed state: conditions and name are
        // populated before the measure section is validated.
        out.spec = FlowSpec{};
        return out;
    };

    std::ifstream in(path);
    if (!in.is_open())
        return fail(FlowErrorCode::FileUnreadable, "cannot open flow file '" + path + "'");

    nlohmann::json root;
    try {
        in >> root;
    } catch (const nlohmann::json::exception &e) {
        return fail(FlowErrorCode::InvalidJson,
                    "invalid JSON in '" + path + "': " + std::string(e.what()));
    }
    if (!root.is_object())
        return fail(FlowErrorCode::WrongShape, "flow file root must be a JSON object");

    if (!root.contains("version"))
        return fail(FlowErrorCode::WrongShape, "missing required field 'version'");
    if (!root["version"].is_number_integer())
        return fail(FlowErrorCode::BadFieldType, "'version' must be an integer");
    if (root["version"].get<int>() != 1)
        return fail(FlowErrorCode::UnsupportedVersion,
                    "unsupported flow version " + std::to_string(root["version"].get<int>()) +
                        " (expected 1)");
    out.spec.version = 1;

    out.spec.name = std::filesystem::path(path).stem().string();
    if (root.contains("name") && !root["name"].is_null()) {
        if (!root["name"].is_string())
            return fail(FlowErrorCode::BadFieldType, "'name' must be a string");
        out.spec.name = root["name"].get<std::string>();
    }

    if (root.contains("conditions") && !root["conditions"].is_null()) {
        if (!root["conditions"].is_array())
            return fail(FlowErrorCode::WrongShape, "'conditions' must be an array");

        size_t index = 0;
        for (const auto &cj : root["conditions"]) {
            const std::string where = "conditions[" + std::to_string(index) + "]";
            if (!cj.is_object())
                return fail(FlowErrorCode::WrongShape, where + " must be an object");
            if (!cj.contains("component"))
                return fail(FlowErrorCode::WrongShape, where + " is missing 'component'");
            if (!cj["component"].is_number_integer())
                return fail(FlowErrorCode::BadFieldType, where + ".component must be an integer");
            if (!cj.contains("path"))
                return fail(FlowErrorCode::WrongShape, where + " is missing 'path'");
            if (!cj["path"].is_string())
                return fail(FlowErrorCode::BadFieldType, where + ".path must be a string");
            if (!cj.contains("values"))
                return fail(FlowErrorCode::WrongShape, where + " is missing 'values'");
            if (!cj["values"].is_array())
                return fail(FlowErrorCode::WrongShape, where + ".values must be an array");
            if (cj["values"].empty())
                return fail(FlowErrorCode::WrongShape, where + ".values must not be empty");

            Condition condition;
            condition.component = cj["component"].get<int>();
            condition.path = cj["path"].get<std::string>();
            for (const auto &vj : cj["values"]) {
                if (!vj.is_number())
                    return fail(FlowErrorCode::BadFieldType,
                                where + ".values must contain only numbers");
                condition.values.push_back(vj.get<double>());
            }
            for (const auto &existing : out.spec.conditions) {
                if (existing.component == condition.component && existing.path == condition.path)
                    return fail(FlowErrorCode::DuplicateConditionTarget,
                                where + ": duplicate condition target '" + condition.path +
                                    "' on component " + std::to_string(condition.component));
            }
            out.spec.conditions.push_back(std::move(condition));
            ++index;
        }
    }

    if (!root.contains("measure") || root["measure"].is_null())
        return fail(FlowErrorCode::WrongShape, "missing required field 'measure'");
    if (!root["measure"].is_array())
        return fail(FlowErrorCode::WrongShape, "'measure' must be an array");
    if (root["measure"].empty())
        return fail(FlowErrorCode::EmptyMeasurement, "'measure' must not be empty");

    size_t index = 0;
    for (const auto &mj : root["measure"]) {
        const std::string where = "measure[" + std::to_string(index) + "]";
        if (!mj.is_object())
            return fail(FlowErrorCode::WrongShape, where + " must be an object");
        if (!mj.contains("component"))
            return fail(FlowErrorCode::WrongShape, where + " is missing 'component'");
        if (!mj["component"].is_number_integer())
            return fail(FlowErrorCode::BadFieldType, where + ".component must be an integer");

        Measurement measurement;
        measurement.component = mj["component"].get<int>();

        if (mj.contains("port") && !mj["port"].is_null()) {
            if (!mj["port"].is_number_integer())
                return fail(FlowErrorCode::BadFieldType, where + ".port must be an integer");
            measurement.port = mj["port"].get<int>();
            if (measurement.port < 0)
                return fail(FlowErrorCode::BadFieldType, where + ".port must not be negative");
        }

        if (!mj.contains("metric"))
            return fail(FlowErrorCode::WrongShape, where + " is missing 'metric'");
        if (!mj["metric"].is_string())
            return fail(FlowErrorCode::BadFieldType, where + ".metric must be a string");
        measurement.metric = mj["metric"].get<std::string>();
        if (MetricRegistry::instance().find(measurement.metric) == nullptr)
            return fail(FlowErrorCode::UnknownMetric, where + ": unknown metric '" +
                                                          measurement.metric +
                                                          "' (known: " + knownMetricNames() + ")");

        // Two readings of the same metric at the same point would collide in the
        // row and in the JSON key, silently dropping one.
        for (const auto &existing : out.spec.measure) {
            if (existing.component == measurement.component && existing.port == measurement.port &&
                existing.metric == measurement.metric)
                return fail(FlowErrorCode::DuplicateMeasurement,
                            where + ": duplicate measurement '" + measurement.metric +
                                "' at component " + std::to_string(measurement.component) +
                                " port " + std::to_string(measurement.port));
        }

        out.spec.measure.push_back(std::move(measurement));
        ++index;
    }

    out.ok = true;
    return out;
}

FlowResult RunFlow(const FlowSpec &spec, std::span<IComponentEngine *const> components,
                   NodeGraphEngine &graph) {
    // 1. Resolve every reference before the first run, so a fatal error still
    //    yields zero rows and a partial experiment can never look complete.
    for (const auto &condition : spec.conditions) {
        IComponentEngine *component = engineById(components, condition.component);
        if (!component)
            return fail(spec, FlowErrorCode::ComponentNotFound,
                        "condition targets unknown component " +
                            std::to_string(condition.component));
        // Every value, not just the first: otherwise a type mismatch would only
        // surface mid-sweep, after some rows had already been computed.
        for (double candidate : condition.values) {
            nlohmann::json snapshot = component->serialize();
            std::string error;
            if (!applyConditionValue(snapshot, condition.path, candidate, &error))
                return fail(spec, FlowErrorCode::PathNotApplicable, error);
        }
    }
    for (const auto &measurement : spec.measure) {
        IComponentEngine *component = engineById(components, measurement.component);
        if (!component)
            return fail(spec, FlowErrorCode::ComponentNotFound,
                        "measurement targets unknown component " +
                            std::to_string(measurement.component));
        if (measurement.port < 0 ||
            static_cast<size_t>(measurement.port) >= component->node().outputs.size())
            return fail(spec, FlowErrorCode::PortOutOfRange,
                        "component " + std::to_string(measurement.component) +
                            " has no output port " + std::to_string(measurement.port));
        if (MetricRegistry::instance().find(measurement.metric) == nullptr)
            return fail(spec, FlowErrorCode::UnknownMetric,
                        "unknown metric '" + measurement.metric + "'");
    }

    // 2. Reject a cyclic circuit. topologicalOrder() appends the nodes it could
    //    not order and still returns a full-length vector, so the cycle has to be
    //    detected from the link order itself; otherwise a flow would silently
    //    produce order-dependent numbers.
    {
        const std::vector<int> order = graph.topologicalOrder();
        std::unordered_map<int, size_t> position;
        position.reserve(order.size());
        for (size_t i = 0; i < order.size(); ++i)
            position[order[i]] = i;

        for (const GraphLink &link : graph.links()) {
            const int from = graph.nodeIdForPin(link.start_pin_id);
            const int to = graph.nodeIdForPin(link.end_pin_id);
            const auto from_it = position.find(from);
            const auto to_it = position.find(to);
            if (from_it == position.end() || to_it == position.end())
                continue;
            if (from_it->second >= to_it->second)
                return fail(spec, FlowErrorCode::CyclicGraph,
                            "circuit has a cycle: node " + std::to_string(from) +
                                " is not ordered before node " + std::to_string(to));
        }
    }

    // 3. Freeze one baseline snapshot per targeted component. Every row patches a
    //    clone of it, so a row never depends on the previous row's state.
    std::vector<std::pair<int, nlohmann::json>> baselines;
    for (const auto &condition : spec.conditions) {
        bool seen = false;
        for (const auto &baseline : baselines) {
            if (baseline.first == condition.component) {
                seen = true;
                break;
            }
        }
        if (seen)
            continue;
        baselines.emplace_back(condition.component,
                               engineById(components, condition.component)->serialize());
    }

    FlowResult result;
    result.name = spec.name;

    const size_t n_conditions = spec.conditions.size();
    std::vector<size_t> counter(n_conditions, 0);
    while (true) {
        FlowRow row;
        row.conditions.reserve(n_conditions);
        row.metrics.reserve(spec.measure.size());

        std::vector<nlohmann::json> working;
        working.reserve(baselines.size());
        for (const auto &baseline : baselines)
            working.push_back(baseline.second);

        for (size_t c = 0; c < n_conditions; ++c) {
            const Condition &condition = spec.conditions[c];
            const double value = condition.values[counter[c]];
            for (size_t b = 0; b < baselines.size(); ++b) {
                if (baselines[b].first != condition.component)
                    continue;
                std::string error;
                if (!applyConditionValue(working[b], condition.path, value, &error))
                    return fail(spec, FlowErrorCode::PathNotApplicable, error);
            }
            row.conditions.push_back({condition.component, condition.path, value});
        }
        for (size_t b = 0; b < baselines.size(); ++b)
            engineById(components, baselines[b].first)->deserialize(working[b]);

        // Mirrors RfSimulatorApp::rewireInputs() by calling the very same
        // function, so the harness and the GUI can never disagree about a
        // circuit's wiring.
        rewireComponentInputs(components, graph);

        for (int node_id : graph.topologicalOrder()) {
            for (auto *component : components) {
                if (component && component->graphNodeId() == node_id) {
                    component->update(0.0);
                    break;
                }
            }
        }

        for (const auto &measurement : spec.measure) {
            IComponentEngine *component = engineById(components, measurement.component);
            const Spectrum &output =
                component->node().outputs[static_cast<size_t>(measurement.port)];
            const MetricDefinition *definition =
                MetricRegistry::instance().find(measurement.metric);
            const double measured = definition->compute(output);

            MetricSample sample;
            sample.name = measurement.metric;
            sample.component = measurement.component;
            sample.port = measurement.port;
            sample.unit = definition->unit;
            sample.value = measured;
            sample.valid = !std::isnan(measured);
            row.metrics.push_back(std::move(sample));
        }
        result.rows.push_back(std::move(row));

        if (n_conditions == 0)
            break;

        // Advance the odometer: the last condition varies fastest. A complete
        // wrap means the last row has already been produced.
        bool wrapped = false;
        size_t k = n_conditions;
        while (k > 0) {
            --k;
            if (++counter[k] < spec.conditions[k].values.size())
                break;
            counter[k] = 0;
            if (k == 0)
                wrapped = true;
        }
        if (wrapped)
            break;
    }

    result.ok = true;
    return result;
}
