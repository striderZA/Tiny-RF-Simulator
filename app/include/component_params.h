#pragma once

#include "component_type_registry.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class IComponentEngine;

enum class ParamWriteStatus {
    Applied,
    Unchanged,
    UnknownComponent,
    UnknownKey,
    PathParamUnsupported,
    ReadOnly,
    TypeMismatch,
    EngineAdjusted,
    DeserializeFailed,
    RestoreFailed,
};

struct ParamChange {
    std::string path;
    nlohmann::json old_value, new_value;
};

struct ParamWriteResult {
    ParamWriteStatus status = ParamWriteStatus::Unchanged;
    std::string path, expected;
    std::vector<std::string> suggestions;
    nlohmann::json requested, stored;
    std::vector<ParamChange> also_changed;
    std::string error;

    bool ok() const;
};

ParamWriteResult applyComponentParams(IComponentEngine &engine,
                                      const std::vector<ParameterField> &state_fields,
                                      const nlohmann::ordered_json &params);
