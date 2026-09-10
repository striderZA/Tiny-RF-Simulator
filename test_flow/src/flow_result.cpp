#include "flow_result.h"
#include <cmath>

namespace {

nlohmann::json encodeValue(double value) {
    return std::isfinite(value) ? nlohmann::json(value) : nlohmann::json(nullptr);
}

} // namespace

nlohmann::json FlowResult::toJson() const {
    nlohmann::json root;
    root["version"] = 1;
    root["name"] = name;
    root["ok"] = ok;

    if (!ok) {
        nlohmann::json error_json;
        error_json["code"] = flowErrorCodeName(error.code);
        error_json["message"] = error.message;
        root["error"] = error_json;
    }

    nlohmann::json rows_json = nlohmann::json::array();
    for (const auto &row : rows) {
        nlohmann::json row_json;

        nlohmann::json conditions_json = nlohmann::json::object();
        for (const auto &condition : row.conditions)
            conditions_json[std::to_string(condition.component) + ":" + condition.path] =
                condition.value;
        row_json["conditions"] = conditions_json;

        nlohmann::json metrics_json = nlohmann::json::object();
        for (const auto &sample : row.metrics) {
            nlohmann::json entry;
            entry["value"] = encodeValue(sample.value);
            entry["unit"] = sample.unit;
            entry["valid"] = sample.valid;
            metrics_json[std::to_string(sample.component) + ":" + std::to_string(sample.port) +
                         ":" + sample.name] = entry;
        }
        row_json["metrics"] = metrics_json;

        rows_json.push_back(row_json);
    }
    root["rows"] = rows_json;
    return root;
}
