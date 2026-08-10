#include "component_library.h"
#include "component_type_registry.h"
#include "logging_core.h"
#include <filesystem>
#include <optional>

#include "amplifier_engine.h"
#include "component_interface.h"
#include "component_registry.h"
#include "node_graph_engine.h"
#include <fstream>

namespace {

namespace fs = std::filesystem;

// --- Data-file path containment (S1) ----------------------------------------
// Mirrors extension_manifest.cpp's resolveWithinRoot discipline: a library
// data-file path is only honored if its canonical form stays inside the
// library JSON file's directory. Absolute paths outside it, '..' traversal,
// and unresolvable paths are skipped instead of reading arbitrary files.

bool containsParentTraversal(const fs::path &path) {
    for (const auto &part : path) {
        if (part == "..")
            return true;
    }
    return false;
}

bool pathWithinRoot(const fs::path &root, const fs::path &candidate) {
    std::error_code ec;
    const fs::path canonical_root = fs::weakly_canonical(root, ec);
    if (ec)
        return false;

    ec.clear();
    const fs::path canonical_candidate = fs::weakly_canonical(candidate, ec);
    if (ec)
        return false;

    auto root_it = canonical_root.begin();
    auto candidate_it = canonical_candidate.begin();
    for (; root_it != canonical_root.end(); ++root_it, ++candidate_it) {
        if (candidate_it == canonical_candidate.end() || *root_it != *candidate_it)
            return false;
    }
    return true;
}

// Resolve a data-file path from a library definition against the library JSON
// file's directory. Returns the canonical path on success, or nullopt when the
// entry must be skipped.
std::optional<fs::path> resolveDataFilePath(const fs::path &json_dir, const std::string &input) {
    const fs::path p(input);
    if (p.empty())
        return std::nullopt;
    if (containsParentTraversal(p))
        return std::nullopt;

    const fs::path candidate = p.is_absolute() ? p : (json_dir / p);
    if (!pathWithinRoot(json_dir, candidate))
        return std::nullopt;

    std::error_code ec;
    const fs::path resolved = fs::weakly_canonical(candidate, ec);
    if (ec)
        return std::nullopt;
    return resolved;
}

} // namespace

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
    } catch (const nlohmann::json::exception &e) {
        // Covers parse_error AND type_error (e.g. a required field present but
        // wrong-typed). A malformed library entry is skipped, not fatal.
        LOG_WARN("ComponentLibrary: invalid JSON in %s: %s", filepath.c_str(), e.what());
        return;
    }
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
    try {
        for (const auto &entry : fs::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                loadFile(entry.path().string());
            }
        }
    } catch (const fs::filesystem_error &e) {
        // An unreadable subtree (e.g. permission denied) must not abort the
        // scan of the remaining roots.
        LOG_WARN("ComponentLibrary: skipping unreadable directory in scan: %s", e.what());
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
                // S1: the data-file path must stay within the library JSON
                // file's directory; absolute paths and '..' escapes are
                // rejected (skipped) rather than reading arbitrary files.
                if (auto sparam_path = resolveDataFilePath(json_dir, df.path)) {
                    amp->setSParamFilepath(sparam_path->string());

                    if (amp->sparamLoaded()) {
                        LOG_INFO("Loaded S-param file for %s: %s", def.part_number.c_str(),
                                 sparam_path->string().c_str());
                    } else {
                        LOG_WARN("Failed to load S-param file for %s: %s (falling back to "
                                 "single-point params)",
                                 def.part_number.c_str(), sparam_path->string().c_str());
                    }
                    break; // Only load first S-param file
                }
                LOG_WARN("ComponentLibrary: rejecting S-param data file path '%s' for %s "
                         "(must stay within the library directory)",
                         df.path.c_str(), def.part_number.c_str());
            }
        }
    }

    if (!def.part_number.empty())
        graph.setNodePartNumber(result->graphNodeId(), def.part_number);
    return result;
}
