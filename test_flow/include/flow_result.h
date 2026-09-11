#pragma once

#include "flow_metrics.h"
#include "flow_types.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct ConditionValue {
    int component = -1;
    std::string path;
    double value = 0.0;
};

struct FlowRow {
    std::vector<ConditionValue> conditions;
    std::vector<MetricSample> metrics;
};

struct FlowResult {
    bool ok = false;
    FlowError error;
    std::string name;
    std::vector<FlowRow> rows;

    // Machine-readable capture. Standard JSON cannot represent NaN/infinity,
    // so non-finite metric values are encoded as JSON null; `valid`
    // distinguishes a measurement from a failure.
    nlohmann::json toJson() const;
};
