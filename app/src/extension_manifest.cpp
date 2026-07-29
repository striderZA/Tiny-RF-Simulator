#include "extension_manifest.h"

#include <fstream>
#include <limits>
#include <system_error>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

void addIssue(std::vector<ExtensionValidationIssue> &issues, std::string field, std::string message) {
    issues.push_back({std::move(field), std::move(message)});
}

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

bool resolveWithinRoot(const fs::path &root,
                       const fs::path &input,
                       fs::path &resolved,
                       std::vector<ExtensionValidationIssue> &issues,
                       const std::string &field) {
    if (input.empty()) {
        addIssue(issues, field, "Path must not be empty");
        return false;
    }

    if (containsParentTraversal(input)) {
        addIssue(issues, field, "Path must not contain '..'");
        return false;
    }

    fs::path candidate = input.is_absolute() ? input : (root / input);
    if (!pathWithinRoot(root, candidate)) {
        addIssue(issues, field, "Path must stay within the extension root");
        return false;
    }

    std::error_code ec;
    resolved = fs::weakly_canonical(candidate, ec);
    if (ec) {
        addIssue(issues, field, "Path could not be resolved");
        return false;
    }

    return true;
}

bool parseCapability(const json &value, ExtensionCapability &out) {
    if (!value.is_string())
        return false;

    const std::string name = value.get<std::string>();
    if (name == "generator") {
        out = ExtensionCapability::Generator;
        return true;
    }
    if (name == "importer") {
        out = ExtensionCapability::Importer;
        return true;
    }
    return false;
}

bool parseMenus(const json &value, std::vector<ExtensionMenuEntry> &menus,
                std::vector<ExtensionValidationIssue> &issues) {
    if (value.is_null())
        return true;
    if (!value.is_array()) {
        addIssue(issues, "menus", "Expected an array");
        return false;
    }

    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto &entry = value[i];
        if (!entry.is_object()) {
            addIssue(issues, "menus[" + std::to_string(i) + "]", "Expected an object");
            continue;
        }

        if (!entry.contains("location") || !entry["location"].is_string()) {
            addIssue(issues, "menus[" + std::to_string(i) + "].location", "Expected a string");
            continue;
        }
        if (!entry.contains("label") || !entry["label"].is_string()) {
            addIssue(issues, "menus[" + std::to_string(i) + "].label", "Expected a string");
            continue;
        }

        menus.push_back({entry["location"].get<std::string>(), entry["label"].get<std::string>()});
    }

    return true;
}

bool parsePathList(const json &value,
                   const fs::path &root,
                   std::vector<fs::path> &paths,
                   std::vector<ExtensionValidationIssue> &issues,
                   const std::string &field) {
    if (value.is_null())
        return true;
    if (!value.is_array()) {
        addIssue(issues, field, "Expected an array");
        return false;
    }

    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto &item = value[i];
        if (!item.is_string()) {
            addIssue(issues, field + "[" + std::to_string(i) + "]", "Expected a string");
            continue;
        }

        fs::path resolved;
        if (resolveWithinRoot(root, fs::path(item.get<std::string>()), resolved, issues,
                              field + "[" + std::to_string(i) + "]")) {
            paths.push_back(std::move(resolved));
        }
    }

    return true;
}

} // namespace

std::optional<ExtensionManifest> parseExtensionManifest(
    const fs::path &manifest_path,
    std::vector<ExtensionValidationIssue> &issues) {
    std::error_code ec;
    const fs::path canonical_manifest_path = fs::weakly_canonical(manifest_path, ec);
    if (ec) {
        addIssue(issues, "manifest", "Could not resolve manifest path");
        return std::nullopt;
    }

    std::ifstream in(canonical_manifest_path);
    if (!in) {
        addIssue(issues, "manifest", "Could not open manifest file");
        return std::nullopt;
    }

    json j = json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        addIssue(issues, "manifest", "Invalid JSON object");
        return std::nullopt;
    }

    ExtensionManifest manifest;
    manifest.manifest_path = canonical_manifest_path;
    manifest.root_dir = canonical_manifest_path.parent_path();

    if (!j.contains("schema_version") || !j["schema_version"].is_number_integer()) {
        addIssue(issues, "schema_version", "Expected an integer");
    } else {
        const auto &schema_version_json = j["schema_version"];
        int schema_version_value = 0;
        bool schema_version_in_range = true;

        if (schema_version_json.is_number_unsigned()) {
            const auto raw = schema_version_json.get<unsigned long long>();
            if (raw > static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
                addIssue(issues, "schema_version", "Integer value is out of range");
                schema_version_in_range = false;
            } else {
                schema_version_value = static_cast<int>(raw);
            }
        } else {
            const auto raw = schema_version_json.get<long long>();
            if (raw < static_cast<long long>(std::numeric_limits<int>::min()) ||
                raw > static_cast<long long>(std::numeric_limits<int>::max())) {
                addIssue(issues, "schema_version", "Integer value is out of range");
                schema_version_in_range = false;
            } else {
                schema_version_value = static_cast<int>(raw);
            }
        }

        if (schema_version_in_range) {
            manifest.schema_version = schema_version_value;
            if (manifest.schema_version != 1)
                addIssue(issues, "schema_version", "Only schema_version=1 is supported");
        }
    }

    auto readRequiredString = [&](const char *field, std::string &out) {
        if (!j.contains(field) || !j[field].is_string()) {
            addIssue(issues, field, "Expected a string");
            return;
        }
        out = j[field].get<std::string>();
        if (out.empty())
            addIssue(issues, field, "Must not be empty");
    };

    readRequiredString("id", manifest.id);
    readRequiredString("name", manifest.name);
    readRequiredString("version", manifest.version);

    if (j.contains("description") && j["description"].is_string())
        manifest.description = j["description"].get<std::string>();
    else if (j.contains("description"))
        addIssue(issues, "description", "Expected a string");

    if (j.contains("author") && j["author"].is_string())
        manifest.author = j["author"].get<std::string>();
    else if (j.contains("author"))
        addIssue(issues, "author", "Expected a string");

    if (j.contains("compat")) {
        if (!j["compat"].is_object()) {
            addIssue(issues, "compat", "Expected an object");
        } else if (j["compat"].contains("min_app_version")) {
            if (!j["compat"]["min_app_version"].is_string()) {
                addIssue(issues, "compat.min_app_version", "Expected a string");
            } else {
                manifest.min_app_version = j["compat"]["min_app_version"].get<std::string>();
            }
        }
    }

    std::string kind_value;
    if (!j.contains("kind") || !j["kind"].is_string()) {
        addIssue(issues, "kind", "Expected 'data-pack' or 'external-tool'");
    } else {
        kind_value = j["kind"].get<std::string>();
        if (kind_value == "data-pack") {
            manifest.kind = ExtensionKind::DataPack;
        } else if (kind_value == "external-tool") {
            manifest.kind = ExtensionKind::ExternalTool;
        } else {
            addIssue(issues, "kind", "Expected 'data-pack' or 'external-tool'");
        }
    }

    if (j.contains("capabilities")) {
        if (!j["capabilities"].is_array()) {
            addIssue(issues, "capabilities", "Expected an array");
        } else {
            for (std::size_t i = 0; i < j["capabilities"].size(); ++i) {
                ExtensionCapability capability{};
                if (!parseCapability(j["capabilities"][i], capability)) {
                    addIssue(issues, "capabilities[" + std::to_string(i) + "]",
                             "Expected 'generator' or 'importer'");
                } else {
                    manifest.capabilities.push_back(capability);
                }
            }
        }
    }

    if (j.contains("menus"))
        parseMenus(j["menus"], manifest.menus, issues);

    if (j.contains("library_roots"))
        parsePathList(j["library_roots"], manifest.root_dir, manifest.library_roots, issues,
                      "library_roots");
    if (j.contains("data_roots"))
        parsePathList(j["data_roots"], manifest.root_dir, manifest.data_roots, issues,
                      "data_roots");

    if (j.contains("entry")) {
        if (!j["entry"].is_string()) {
            addIssue(issues, "entry", "Expected a string");
        } else {
            fs::path resolved_entry;
            if (resolveWithinRoot(manifest.root_dir, fs::path(j["entry"].get<std::string>()),
                                  resolved_entry, issues, "entry")) {
                manifest.entry_path = std::move(resolved_entry);
            }
        }
    } else if (manifest.kind == ExtensionKind::ExternalTool) {
        addIssue(issues, "entry", "Required for external-tool manifests");
    }

    if (!issues.empty())
        return std::nullopt;

    return manifest;
}
