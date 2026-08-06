#include "component_library.h"
#include "component_type_registry.h"
#include "logging_core.h"
#include <filesystem>

#include "amplifier_engine.h"
#include "component_interface.h"
#include "component_registry.h"
#include "node_graph_engine.h"
#include <fstream>

std::vector<ValidationIssue> ComponentLibrary::validate(const std::string &type,
                                                        const nlohmann::json &parameters) const {
    std::vector<ValidationIssue> issues;
    const auto *descriptor = ComponentTypeRegistry::instance().find(type);
    if (!descriptor) {
        issues.push_back({"", "Unknown component type '" + type + "'"});
        return issues;
    }
    for (const auto &field : descriptor->fields) {
        bool present = parameters.contains(field.key);
        if (!present) {
            if (field.required)
                issues.push_back({field.key, "'" + field.label + "' is required"});
            continue;
        }
        const auto &v = parameters[field.key];
        switch (field.kind) {
        case FieldKind::Number: {
            if (!v.is_number()) {
                issues.push_back({field.key, "'" + field.label + "' must be a number"});
                break;
            }
            double d = v.get<double>();
            if (d < field.min || d > field.max) {
                issues.push_back({field.key, "'" + field.label + "' must be between " +
                                                 std::to_string(field.min) + " and " +
                                                 std::to_string(field.max)});
            }
            break;
        }
        case FieldKind::String:
        case FieldKind::FilePath:
            if (!v.is_string())
                issues.push_back({field.key, "'" + field.label + "' must be text"});
            break;
        case FieldKind::Bool:
            if (!v.is_boolean())
                issues.push_back({field.key, "'" + field.label + "' must be true/false"});
            break;
        case FieldKind::Enum: {
            if (!v.is_string()) {
                issues.push_back({field.key, "'" + field.label + "' must be text"});
                break;
            }
            std::string s = v.get<std::string>();
            bool valid = false;
            for (const auto &ev : field.enum_values)
                if (ev == s)
                    valid = true;
            if (!valid)
                issues.push_back(
                    {field.key, "'" + field.label + "' has an invalid value '" + s + "'"});
            break;
        }
        }
    }
    return issues;
}

void ComponentLibrary::upsert(const ComponentDefinition &def) {
    for (auto &existing : m_definitions) {
        if (existing.source_path == def.source_path) {
            existing = def;
            return;
        }
    }
    m_definitions.push_back(def);
}

void ComponentLibrary::loadFile(const std::string &filepath) {
    std::ifstream ifs(filepath);
    if (!ifs.is_open()) {
        LOG_WARN("ComponentLibrary: cannot open file: %s", filepath.c_str());
        return;
    }

    nlohmann::json j;
    try {
        ifs >> j;
    } catch (const nlohmann::json::parse_error &e) {
        LOG_WARN("ComponentLibrary: JSON parse error in %s: %s", filepath.c_str(), e.what());
        return;
    }

    if (!j.contains("type") || !j.contains("part_number") || !j.contains("parameters")) {
        LOG_WARN("ComponentLibrary: missing required fields in %s", filepath.c_str());
        return;
    }

    ComponentDefinition def;
    def.schema_version = j.value("schema_version", 1);
    def.type = j["type"].get<std::string>();
    def.part_number = j["part_number"].get<std::string>();
    def.manufacturer = j.value("manufacturer", "");
    def.description = j.value("description", "");
    def.parameters = j["parameters"];
    def.test_conditions = j.value("test_conditions", nlohmann::json::object());
    def.notes = j.value("notes", "");
    def.source_path = filepath;
    def.issues = validate(def.type, def.parameters);

    // Parse data_files array if present
    if (j.contains("data_files") && j["data_files"].is_array()) {
        for (const auto &df : j["data_files"]) {
            if (df.contains("type") && df.contains("path")) {
                def.data_files.push_back(
                    {df["type"].get<std::string>(), df["path"].get<std::string>()});
            }
        }
    }

    m_definitions.push_back(std::move(def));
}

std::vector<const ComponentDefinition *> ComponentLibrary::all() const {
    std::vector<const ComponentDefinition *> result;
    result.reserve(m_definitions.size());
    for (const auto &def : m_definitions) {
        result.push_back(&def);
    }
    return result;
}

void ComponentLibrary::scan(const std::string &directory) {
    namespace fs = std::filesystem;
    if (!fs::exists(directory))
        return;
    for (const auto &entry : fs::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            loadFile(entry.path().string());
        }
    }
}

std::vector<const ComponentDefinition *> ComponentLibrary::byType(const std::string &type) const {
    std::vector<const ComponentDefinition *> result;
    for (const auto &def : m_definitions) {
        if (def.type == type)
            result.push_back(&def);
    }
    return result;
}

IComponentEngine *ComponentLibrary::instantiate(const ComponentDefinition &def, int id,
                                                ComponentRegistry &registry,
                                                NodeGraphEngine &graph) {
    const auto *descriptor = ComponentTypeRegistry::instance().find(def.type);
    if (!descriptor) {
        LOG_WARN("ComponentLibrary: unknown component type '%s'", def.type.c_str());
        return nullptr;
    }

    IComponentEngine *result = descriptor->create(registry, graph, id);
    if (!result)
        return nullptr;
    result->deserialize(def.parameters);

    if (def.type == "amplifier") {
        auto *amp = dynamic_cast<AmplifierEngine *>(result);
        for (const auto &df : def.data_files) {
            if (df.type == "s_parameters" && amp) {
                std::filesystem::path json_dir =
                    std::filesystem::path(def.source_path).parent_path();
                std::filesystem::path sparam_path = json_dir / df.path;
                amp->setSParamFilepath(sparam_path.string());

                if (amp->sparamLoaded()) {
                    LOG_INFO("Loaded S-param file for %s: %s", def.part_number.c_str(),
                             sparam_path.string().c_str());
                } else {
                    LOG_WARN("Failed to load S-param file for %s: %s (falling back to "
                             "single-point params)",
                             def.part_number.c_str(), sparam_path.string().c_str());
                }
                break; // Only load first S-param file
            }
        }
    }

    if (!def.part_number.empty())
        graph.setNodePartNumber(result->graphNodeId(), def.part_number);
    return result;
}
