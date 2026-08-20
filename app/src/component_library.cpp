#include "component_library.h"
#include "component_type_registry.h"
#include "logging_core.h"
#include <exception>
#include <filesystem>
#include <optional>
#include <system_error>

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

        // Explicit required-field validation: a syntactically valid but
        // wrong-typed required field is a malformed entry, not a definition.
        if (!j.is_object() || !j.contains("type") || !j["type"].is_string() ||
            !j.contains("part_number") || !j["part_number"].is_string() ||
            !j.contains("parameters") || !j["parameters"].is_object()) {
            LOG_WARN("ComponentLibrary: invalid required field types in %s", filepath.c_str());
            return;
        }

        ComponentDefinition def;
        if (j.contains("schema_version") && j["schema_version"].is_number_integer()) {
            def.schema_version = j["schema_version"].get<int>();
        } else {
            if (j.contains("schema_version")) {
                LOG_WARN("ComponentLibrary: schema_version is not an integer in %s",
                         filepath.c_str());
            }
            def.schema_version = 1;
        }
        def.type = j["type"].get<std::string>();
        def.part_number = j["part_number"].get<std::string>();

        // Optional string fields: leave the default empty string when the
        // value is present but not a string.
        const auto optionalString = [&](const char *key) {
            return j.contains(key) && j[key].is_string() ? j[key].get<std::string>()
                                                         : std::string{};
        };
        def.manufacturer = optionalString("manufacturer");
        def.description = optionalString("description");
        def.notes = optionalString("notes");

        def.parameters = j["parameters"];
        def.test_conditions = j.value("test_conditions", nlohmann::json::object());
        def.source_path = filepath;
        def.issues = validate(def.type, def.parameters);

        // Parse data_files, isolating malformed entries: only array entries
        // that are objects with string type/path are kept.
        if (j.contains("data_files")) {
            if (!j["data_files"].is_array()) {
                LOG_WARN("ComponentLibrary: data_files is not an array in %s", filepath.c_str());
            } else {
                for (std::size_t i = 0; i < j["data_files"].size(); ++i) {
                    const auto &df = j["data_files"][i];
                    if (!df.is_object() || !df.contains("type") || !df["type"].is_string() ||
                        !df.contains("path") || !df["path"].is_string()) {
                        LOG_WARN("ComponentLibrary: skipping malformed data_files[%zu] in %s", i,
                                 filepath.c_str());
                        continue;
                    }
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
    } catch (const std::exception &e) {
        // A non-JSON exception (e.g. from the parser or filesystem layer)
        // stays inside the file boundary so sibling files are still visited.
        LOG_WARN("ComponentLibrary: failed to load %s: %s", filepath.c_str(), e.what());
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
    std::error_code ec;
    if (!fs::exists(directory, ec) || ec)
        return;
    try {
        fs::recursive_directory_iterator it(directory,
                                            fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;
        if (ec) {
            LOG_WARN("ComponentLibrary: cannot scan %s: %s", directory.c_str(),
                     ec.message().c_str());
            return;
        }
        for (; it != end; it.increment(ec)) {
            if (ec) {
                LOG_WARN("ComponentLibrary: scan error in %s: %s", it->path().string().c_str(),
                         ec.message().c_str());
                ec.clear();
                continue;
            }
            if (it->is_regular_file() && it->path().extension() == ".json") {
                loadFile(it->path().string());
            }
        }
        if (ec) {
            LOG_WARN("ComponentLibrary: scan ended with error in %s: %s", directory.c_str(),
                     ec.message().c_str());
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
