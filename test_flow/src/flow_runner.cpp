#include "flow_runner.h"

#include "flow_metrics.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

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
