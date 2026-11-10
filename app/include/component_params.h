#pragma once

#include "component_type_registry.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class IComponentEngine;

// Serialized keys whose values name data files. Project load and library instantiation
// resolve and containment-check them, component authoring protects their files, and
// agent and inspector writes never set them.
inline constexpr const char *kPathParameterKeys[] = {"sparam_filepath", "sparam_path",
                                                     "amp_filepath", "ampf_filepath"};

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

// One clause naming the key, for a refusal sentence: "unknown key 'gian_dB'", "'atten_dB' must be
// number from 0 to 100", "the engine changed 'preset_index' from 0 to 4",
// "'sparam_filepath' names a data file; idealized stages take no files". Empty when the write
// applied or changed nothing.
std::string paramWriteMessage(const ParamWriteResult &result);
