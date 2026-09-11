#pragma once

#include <string>
#include <vector>

// Canonical error codes for the test-flow harness. Any code other than None
// means the flow produced no rows at all: the harness fails loudly rather than
// emitting partial or NaN-filled results.
enum class FlowErrorCode {
    None,
    FileUnreadable,
    InvalidJson,
    UnsupportedVersion,
    WrongShape,
    BadFieldType,
    EmptyMeasurement,
    UnknownMetric,
    ComponentNotFound,
    PortOutOfRange,
    PathNotApplicable,
    DuplicateConditionTarget,
    DuplicateMeasurement,
    CyclicGraph,
};

inline const char *flowErrorCodeName(FlowErrorCode code) {
    switch (code) {
    case FlowErrorCode::None:
        return "none";
    case FlowErrorCode::FileUnreadable:
        return "file_unreadable";
    case FlowErrorCode::InvalidJson:
        return "invalid_json";
    case FlowErrorCode::UnsupportedVersion:
        return "unsupported_version";
    case FlowErrorCode::WrongShape:
        return "wrong_shape";
    case FlowErrorCode::BadFieldType:
        return "bad_field_type";
    case FlowErrorCode::EmptyMeasurement:
        return "empty_measurement";
    case FlowErrorCode::UnknownMetric:
        return "unknown_metric";
    case FlowErrorCode::ComponentNotFound:
        return "component_not_found";
    case FlowErrorCode::PortOutOfRange:
        return "port_out_of_range";
    case FlowErrorCode::PathNotApplicable:
        return "path_not_applicable";
    case FlowErrorCode::DuplicateConditionTarget:
        return "duplicate_condition_target";
    case FlowErrorCode::DuplicateMeasurement:
        return "duplicate_measurement";
    case FlowErrorCode::CyclicGraph:
        return "cyclic_graph";
    }
    return "unknown";
}

struct FlowError {
    FlowErrorCode code = FlowErrorCode::None;
    std::string message;
};

// One swept input. `path` addresses the target engine's serialize() object
// (e.g. "gain_dB", "tones[0].power_dBm"); one run happens per entry in `values`.
struct Condition {
    int component = -1;
    std::string path;
    std::vector<double> values;
};

// One output reading, from `component`'s output port `port`.
struct Measurement {
    int component = -1;
    int port = 0;
    std::string metric;
};

struct FlowSpec {
    int version = 1;
    std::string name;
    std::vector<Condition> conditions;
    std::vector<Measurement> measure;
};
