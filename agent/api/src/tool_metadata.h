#pragma once

#include "component_type_registry.h"

#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace agent_api_detail {

inline nlohmann::json parameterInfo(const ComponentTypeDescriptor &descriptor) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto &field : descriptor.state_fields) {
        std::string kind;
        switch (field.kind) {
        case FieldKind::Number:
            kind = "number";
            break;
        case FieldKind::String:
            kind = "string";
            break;
        case FieldKind::Enum:
            kind = "enum";
            break;
        case FieldKind::FilePath:
            kind = "file_path";
            break;
        case FieldKind::Bool:
            kind = "boolean";
            break;
        }
        result.push_back({{"path", field.key},
                          {"kind", std::move(kind)},
                          {"unit", field.unit},
                          {"enum_labels", field.enum_values},
                          {"help", field.help},
                          {"read_only", field.read_only}});
    }
    return result;
}

} // namespace agent_api_detail
