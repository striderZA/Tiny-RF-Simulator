#include "component_library.h"
#include "component_params.h"
#include "component_type_registry.h"
#include "logging_core.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "amplifier_engine.h"
#include "circuit_runtime.h"
#include "component_interface.h"
#include "graph_editor_actions.h"
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

// True only for a bare file name that cannot escape the directory it is joined
// to: non-empty, no '/' or '\' (rejected on both platforms so a library
// authored on one is safe on the other), no drive/root prefix and no "."/".."
// component. Deliberately stricter than resolveDataFilePath(), which also
// honors contained subdirectory paths — a *copied* file has no reason to name
// one.
bool isSafeDataFileName(const std::string &path) {
    if (path.empty())
        return false;
    if (path.find('/') != std::string::npos || path.find('\\') != std::string::npos)
        return false;
    const fs::path p(path);
    if (p.is_absolute() || p.has_root_name() || p.has_root_directory() || p.has_parent_path())
        return false;
    for (const auto &part : p) {
        if (part == "." || part == "..")
            return false;
    }
    return !p.filename().empty();
}

// --- Ideal filter cutoffs (issue #181) ---------------------------------------
// The filter's two cutoff fields are each optional in the schema, but
// IdealFilterEngine reads them per filter type: LPF/HPF use fc_low_Hz as their
// single cutoff, BPF/BSF the fc_low_Hz..fc_high_Hz band. A definition must
// carry the cutoffs its type uses, or deserialize() silently keeps the engine's
// constructor defaults. Labels match what the authoring form shows per type.
void appendFilterCutoffIssues(const nlohmann::json &parameters,
                              std::vector<ValidationIssue> &issues) {
    if (!parameters.contains("filter_type") || !parameters["filter_type"].is_string())
        return; // reported by the per-field checks
    const std::string filter_type = parameters["filter_type"].get<std::string>();
    const bool has_low = parameters.contains("fc_low_Hz");
    const bool has_high = parameters.contains("fc_high_Hz");
    if (filter_type == "LPF" || filter_type == "HPF") {
        if (!has_low)
            issues.push_back({"fc_low_Hz", "'Cutoff' is required"});
        return;
    }
    if (filter_type != "BPF" && filter_type != "BSF")
        return; // an unknown filter type is reported by the per-field checks
    if (!has_low)
        issues.push_back({"fc_low_Hz", "'Low Cutoff' is required"});
    if (!has_high)
        issues.push_back({"fc_high_Hz", "'High Cutoff' is required"});
    if (has_low && has_high && parameters["fc_low_Hz"].is_number() &&
        parameters["fc_high_Hz"].is_number() &&
        parameters["fc_low_Hz"].get<double>() >= parameters["fc_high_Hz"].get<double>())
        issues.push_back({"fc_high_Hz", "'High Cutoff' must be greater than 'Low Cutoff'"});
}

std::string normalizedPartNumber(std::string_view part_number) {
    std::string normalized;
    normalized.reserve(part_number.size());
    for (const unsigned char character : part_number)
        normalized.push_back(static_cast<char>(std::tolower(character)));
    return normalized;
}

std::string partNumberFileSegment(std::string_view normalized_part_number) {
    constexpr char kHex[] = "0123456789abcdef";
    std::string segment = "part-";
    if (normalized_part_number.size() <= (segment.max_size() - segment.size()) / 3)
        segment.reserve(segment.size() + normalized_part_number.size() * 3);
    for (const unsigned char character : normalized_part_number) {
        if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
            character == '-' || character == '_') {
            segment.push_back(static_cast<char>(character));
        } else {
            segment.push_back('~');
            segment.push_back(kHex[character >> 4]);
            segment.push_back(kHex[character & 0x0f]);
        }
    }
    return segment;
}

bool isUnsafeLibraryDirectory(const fs::path &path, const fs::file_status &status) {
    if (fs::is_symlink(status))
        return true;

    // Check every prefix: a final-component-only check follows POSIX symlink ancestors, while
    // Windows junctions can appear as ordinary directories.
    const auto isUnsafePrefix = [](const fs::path &prefix) {
#ifdef _WIN32
        const DWORD attributes = GetFileAttributesW(prefix.c_str());
        return attributes == INVALID_FILE_ATTRIBUTES ||
               (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
               (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
        std::error_code ec;
        const fs::file_status prefix_status = fs::symlink_status(prefix, ec);
        return ec || fs::is_symlink(prefix_status) || !fs::is_directory(prefix_status);
#endif
    };

    fs::path prefix = path.root_path();
    if (prefix.empty() || isUnsafePrefix(prefix))
        return true;
    for (const auto &component : path.relative_path()) {
        prefix /= component;
        if (prefix == path) {
#ifdef _WIN32
            if (isUnsafePrefix(prefix))
                return true;
#endif
            continue;
        }
        if (isUnsafePrefix(prefix))
            return true;
    }
    return false;
}

bool ensureLibraryDirectory(const fs::path &path, std::string &error) {
    if (path.empty()) {
        error = "component library directory is empty";
        return false;
    }

    std::error_code ec;
    fs::file_status status = fs::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory ||
        (!ec && status.type() == fs::file_type::not_found)) {
        ec.clear();
        const fs::path parent = path.parent_path();
        const fs::file_status parent_status = fs::symlink_status(parent, ec);
        if (ec || isUnsafeLibraryDirectory(parent, parent_status) ||
            !fs::is_directory(parent_status)) {
            error = "component library parent is not a real directory: " + parent.string();
            return false;
        }
        ec.clear();
        fs::create_directory(path, ec);
        if (ec && ec != std::errc::file_exists) {
            error = "could not create component library directory: " + ec.message();
            return false;
        }
        ec.clear();
        status = fs::symlink_status(path, ec);
    }
    if (ec) {
        error = "could not inspect component library directory: " + ec.message();
        return false;
    }
    if (isUnsafeLibraryDirectory(path, status) || !fs::is_directory(status)) {
        error = "component library path is not a real directory: " + path.string();
        return false;
    }
    return true;
}

} // namespace

std::optional<fs::path> componentDataFilePath(const ComponentDefinition &definition,
                                              std::string_view data_type) {
    if (definition.source_path.empty())
        return std::nullopt;
    const fs::path json_dir = fs::path(definition.source_path).parent_path();
    for (const DataFileRef &ref : definition.data_files) {
        if (ref.type == data_type)
            return resolveDataFilePath(json_dir, ref.path); // first matching entry decides
    }
    return std::nullopt;
}

std::string sanitizePathSegment(const std::string &s, const std::string &fallback) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ' ')
            out.push_back(c);
    }
    size_t start = out.find_first_not_of(' ');
    if (start == std::string::npos)
        return fallback;
    size_t end = out.find_last_not_of(' ');
    out = out.substr(start, end - start + 1);
    return out.empty() ? fallback : out;
}

std::optional<std::filesystem::path> dataFileCopyDestination(const std::string &dest_dir,
                                                             const std::string &name) {
    if (dest_dir.empty() || !isSafeDataFileName(name))
        return std::nullopt;
    return std::filesystem::path(dest_dir) / name;
}

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
    if (type == "filter")
        appendFilterCutoffIssues(parameters, issues);
    return issues;
}

void ComponentLibrary::upsert(const ComponentDefinition &def) {
    // Issue #79: an invalid definition must never enter the in-memory library,
    // where it would surface in the browser (insertion UI). loadFile() drops
    // issue-ful files and the authoring form refuses to save them, so this
    // guards direct callers. Validation is recomputed from type/parameters:
    // the caller-maintained `issues` cache is untrusted — a hand-built
    // definition can carry an empty list for a definition that is not valid.
    const auto issues = validate(def.type, def.parameters);
    if (!issues.empty()) {
        LOG_WARN("ComponentLibrary: refusing to upsert %s (%s): %zu validation issue(s)",
                 def.part_number.c_str(), def.type.c_str(), issues.size());
        return;
    }
    for (auto &existing : m_definitions) {
        if (existing.source_path == def.source_path) {
            existing = def;
            return;
        }
    }
    m_definitions.push_back(def);
}
ComponentLibraryCreateResult
ComponentLibrary::createUserEntry(ComponentDefinition definition, const fs::path &library_root) {
    ComponentLibraryCreateResult result;
    const auto invalidDefinition = [](std::vector<ValidationIssue> issues) {
        ComponentLibraryCreateResult invalid;
        invalid.status = ComponentLibraryCreateStatus::InvalidDefinition;
        invalid.issues = std::move(issues);
        return invalid;
    };
    const auto ioFailure = [](std::string message) {
        ComponentLibraryCreateResult failure;
        failure.status = ComponentLibraryCreateStatus::IoError;
        failure.message = std::move(message);
        LOG_WARN("ComponentLibrary: %s", failure.message.c_str());
        return failure;
    };

    std::vector<ValidationIssue> issues;
    const auto *descriptor = ComponentTypeRegistry::instance().find(definition.type);
    if (!descriptor || !descriptor->authorable) {
        issues.push_back({"type", "Component type is unknown or not authorable"});
        return invalidDefinition(std::move(issues));
    }
    if (definition.part_number.find_first_not_of(" \t\r\n") == std::string::npos)
        issues.push_back({"part_number", "Part number is required"});
    if (!definition.parameters.is_object())
        issues.push_back({"parameters", "Parameters must be an object"});
    if (!definition.data_files.empty())
        issues.push_back({"data_files", "Agent-created entries cannot include data files"});

    if (definition.parameters.is_object()) {
        for (const auto &[key, value] : definition.parameters.items()) {
            static_cast<void>(value);
            bool path_parameter = false;
            for (const char *path_key : kPathParameterKeys)
                path_parameter = path_parameter || key == path_key;
            if (path_parameter) {
                issues.push_back({key, "File-path parameters are not supported"});
                continue;
            }

            const auto field = std::find_if(
                descriptor->fields.begin(), descriptor->fields.end(),
                [&key](const ParameterField &candidate) { return candidate.key == key; });
            if (field == descriptor->fields.end()) {
                issues.push_back({key, "Unknown component-library parameter"});
            } else if (field->kind == FieldKind::FilePath) {
                issues.push_back({key, "File-path parameters are not supported"});
            }
        }
    }

    auto parameter_issues = validate(definition.type, definition.parameters);
    issues.insert(issues.end(), std::make_move_iterator(parameter_issues.begin()),
                  std::make_move_iterator(parameter_issues.end()));
    if (!issues.empty())
        return invalidDefinition(std::move(issues));

    const std::string wanted_part = normalizedPartNumber(definition.part_number);
    for (const auto &existing : m_definitions) {
        if (existing.type == definition.type &&
            normalizedPartNumber(existing.part_number) == wanted_part) {
            result.status = ComponentLibraryCreateStatus::AlreadyExists;
            result.message = "A component with this type and part number already exists";
            return result;
        }
    }

    nlohmann::json serialized{{"schema_version", 2},
                              {"type", definition.type},
                              {"part_number", definition.part_number},
                              {"manufacturer", definition.manufacturer},
                              {"description", definition.description},
                              {"parameters", definition.parameters},
                              {"notes", definition.notes}};
    const std::string contents = serialized.dump(2);

    m_definitions.reserve(m_definitions.size() + 1);

    // Inspect the supplied path before absolute() may normalize `..` on Windows.
    for (const auto &component : library_root) {
        if (component == "..")
            return ioFailure("component library root contains parent traversal");
    }

    std::error_code ec;
    const fs::path absolute_root = fs::absolute(library_root, ec);
    if (ec)
        return ioFailure("could not resolve component library root: " + ec.message());
    for (const auto &component : absolute_root) {
        if (component == "..")
            return ioFailure("component library root contains parent traversal");
    }
    const fs::path root = absolute_root.lexically_normal();
    std::string directory_error;
    if (!ensureLibraryDirectory(root.parent_path(), directory_error) ||
        !ensureLibraryDirectory(root, directory_error))
        return ioFailure(std::move(directory_error));

    const fs::path type_directory = root / definition.type;
    if (!ensureLibraryDirectory(type_directory, directory_error))
        return ioFailure(std::move(directory_error));

    const fs::path agent_directory = type_directory / ".mcp";
    if (!ensureLibraryDirectory(agent_directory, directory_error))
        return ioFailure(std::move(directory_error));

    // A normalized, escaped part-number filename makes the no-replace hard link below reject
    // concurrent duplicate MCP creations even when their manufacturers differ.
    const fs::path destination =
        agent_directory / (partNumberFileSegment(wanted_part) + ".json");

    const fs::file_status destination_status = fs::symlink_status(destination, ec);
    if (ec == std::errc::no_such_file_or_directory) {
        ec.clear();
    } else if (ec) {
        return ioFailure("could not inspect component destination: " + ec.message());
    } else if (destination_status.type() != fs::file_type::not_found) {
        result.status = ComponentLibraryCreateStatus::AlreadyExists;
        result.message = "A component file already exists at the destination";
        return result;
    }

    static std::atomic<unsigned long long> staging_sequence{0};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path staging_directory;
    bool staging_directory_created = false;
    for (int attempt = 0; attempt < 16; ++attempt) {
        staging_directory =
            type_directory /
            (".component-create-" + std::to_string(timestamp) + "-" +
             std::to_string(staging_sequence.fetch_add(1)));
        ec.clear();
        staging_directory_created = fs::create_directory(staging_directory, ec);
        if (staging_directory_created)
            break;
        if (ec && ec != std::errc::file_exists)
            return ioFailure("could not reserve component staging directory: " + ec.message());
    }
    if (!staging_directory_created)
        return ioFailure("could not reserve a unique component staging directory");

    const fs::path staging_file = staging_directory / "definition.tmp";
    const auto cleanupStaging = [&] {
        std::error_code cleanup_error;
        fs::remove(staging_file, cleanup_error);
        cleanup_error.clear();
        fs::remove(staging_directory, cleanup_error);
        if (cleanup_error)
            LOG_WARN("ComponentLibrary: could not remove staging directory %s: %s",
                     staging_directory.string().c_str(), cleanup_error.message().c_str());
    };

    std::ofstream output(staging_file, std::ios::binary);
    if (!output) {
        cleanupStaging();
        return ioFailure("could not open temporary component definition");
    }
    output << contents;
    output.flush();
    if (!output) {
        output.close();
        cleanupStaging();
        return ioFailure("could not write temporary component definition");
    }
    output.close();
    if (!output) {
        cleanupStaging();
        return ioFailure("could not close temporary component definition");
    }

    ec.clear();
    fs::create_hard_link(staging_file, destination, ec);
    if (ec) {
        std::error_code status_error;
        const fs::file_status current_status = fs::symlink_status(destination, status_error);
        cleanupStaging();
        if (!status_error && current_status.type() != fs::file_type::not_found) {
            result.status = ComponentLibraryCreateStatus::AlreadyExists;
            result.message = "A component file already exists at the destination";
            return result;
        }
        return ioFailure("could not publish component definition: " + ec.message());
    }

    definition.schema_version = 2;
    definition.source_path = destination.string();
    definition.issues.clear();
    try {
        m_definitions.push_back(std::move(definition));
    } catch (...) {
        std::error_code remove_error;
        fs::remove(destination, remove_error);
        cleanupStaging();
        throw;
    }
    cleanupStaging();

    result.status = ComponentLibraryCreateStatus::Created;
    result.path = destination;
    return result;
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
            // A JSON integer can exceed int range; verify representability
            // before get<int>(), which would throw and discard an
            // otherwise-valid definition. Mirrors extension_manifest.cpp.
            const auto in_range = [&]() {
                if (j["schema_version"].is_number_unsigned()) {
                    return j["schema_version"].get<unsigned long long>() <=
                           static_cast<unsigned long long>(std::numeric_limits<int>::max());
                }
                const auto raw = j["schema_version"].get<long long>();
                return raw >= static_cast<long long>(std::numeric_limits<int>::min()) &&
                       raw <= static_cast<long long>(std::numeric_limits<int>::max());
            }();
            if (in_range) {
                def.schema_version = j["schema_version"].get<int>();
            } else {
                LOG_WARN("ComponentLibrary: schema_version is out of int range in %s",
                         filepath.c_str());
                def.schema_version = 1;
            }
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

        // Issue #79: reject, don't retain, definitions whose parameters fail
        // type/range/enum validation (or whose type is unknown). They must
        // never reach the insertion UI or engine factory; the file is logged
        // and skipped so valid siblings still load.
        if (!def.issues.empty()) {
            LOG_WARN("ComponentLibrary: rejecting definition '%s' in %s (%zu validation "
                     "issue(s))",
                     def.part_number.c_str(), filepath.c_str(), def.issues.size());
            for (const auto &issue : def.issues) {
                const std::string location = issue.field.empty() ? "definition" : issue.field;
                LOG_WARN("ComponentLibrary:   - %s: %s", location.c_str(), issue.message.c_str());
            }
            return;
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
    if (!fs::exists(directory, ec) || ec) {
        if (ec)
            LOG_WARN("ComponentLibrary: cannot access scan root %s: %s", directory.c_str(),
                     ec.message().c_str());
        return;
    }
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
            std::error_code entry_ec;
            if (it->is_regular_file(entry_ec) && it->path().extension() == ".json") {
                loadFile(it->path().string());
            }
            if (entry_ec) {
                LOG_WARN("ComponentLibrary: status error for %s: %s", it->path().string().c_str(),
                         entry_ec.message().c_str());
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

IComponentEngine *ComponentLibrary::instantiate(const ComponentDefinition &def,
                                                CircuitRuntime &runtime,
                                                GraphEditorActions &editor_actions,
                                                std::optional<int> component_id) {
    const auto *descriptor = ComponentTypeRegistry::instance().find(def.type);
    if (!descriptor) {
        LOG_WARN("ComponentLibrary: unknown component type '%s'", def.type.c_str());
        return nullptr;
    }
    // Issue #79: the library never stores issue-ful definitions (loadFile and
    // upsert reject them), so an invalid definition reaching the factory is a
    // direct-caller bug; refuse instead of creating a broken engine. Validation
    // is recomputed from type/parameters — never trust the caller-maintained
    // `issues` cache, which a hand-built definition can leave empty.
    const auto issues = validate(def.type, def.parameters);
    if (!issues.empty()) {
        LOG_WARN("ComponentLibrary: refusing to instantiate '%s' (%s): %zu validation "
                 "issue(s)",
                 def.part_number.c_str(), def.type.c_str(), issues.size());
        return nullptr;
    }

    IComponentEngine *result =
        component_id ? runtime.createComponentWithId(descriptor->create, *component_id)
                     : runtime.createComponent(descriptor->create);
    if (!result)
        return nullptr;

    // The runtime factory registers the engine in its owned registry and graph;
    // any failure below rolls it back through runtime removal so no partial
    // component survives and no post-creation exception escapes to the caller.
    try {
        const std::filesystem::path json_dir = std::filesystem::path(def.source_path).parent_path();
        // Issue #79: path-bearing *parameters* are resolved against the library
        // JSON's directory and containment-checked like data_files entries;
        // escaping paths are dropped so engine deserialize() never loads an
        // arbitrary file (mirrors the project-file load boundary).
        nlohmann::json params = def.parameters;
        if (params.is_object() && !def.source_path.empty()) {
            for (const char *key : kPathParameterKeys) {
                if (!params.contains(key) || !params[key].is_string())
                    continue;
                const std::string value = params[key].get<std::string>();
                if (auto resolved = resolveDataFilePath(json_dir, value)) {
                    params[key] = resolved->string();
                } else {
                    LOG_WARN("ComponentLibrary: rejecting path parameter '%s' for "
                             "%s (must stay within the library directory)",
                             value.c_str(), def.part_number.c_str());
                    params.erase(key);
                }
            }
        }
        result->deserialize(params);

        bool sparam_data_file_consumed = false;
        for (const auto &df : def.data_files) {
            if (df.type != "s_parameters")
                continue;
            if (!descriptor->supports_sparam_file) {
                LOG_WARN("ComponentLibrary: ignoring S-param data file '%s' for unsupported "
                         "component type '%s' (%s)",
                         df.path.c_str(), def.type.c_str(), def.part_number.c_str());
                continue;
            }
            if (!descriptor->load_sparam_file) {
                LOG_WARN("ComponentLibrary: no S-param loader registered for '%s' (%s)",
                         def.type.c_str(), def.part_number.c_str());
                continue;
            }
            if (sparam_data_file_consumed) {
                LOG_WARN("ComponentLibrary: ignoring additional S-param data file '%s' for %s "
                         "(only one S-param file can be loaded)",
                         df.path.c_str(), def.part_number.c_str());
                continue;
            }

            // Keep data-file reads confined to the library JSON's directory;
            // absolute paths and '..' escapes are rejected rather than used.
            const auto sparam_path = resolveDataFilePath(json_dir, df.path);
            if (!sparam_path) {
                LOG_WARN("ComponentLibrary: rejecting S-param data file path '%s' for %s "
                         "(must stay within the library directory)",
                         df.path.c_str(), def.part_number.c_str());
                continue;
            }

            sparam_data_file_consumed = true;
            if (descriptor->load_sparam_file(*result, sparam_path->string())) {
                LOG_INFO("Loaded S-param file for %s: %s", def.part_number.c_str(),
                         sparam_path->string().c_str());
            } else {
                LOG_WARN("Failed to load S-param file for %s: %s (S-param mode remains disabled)",
                         def.part_number.c_str(), sparam_path->string().c_str());
            }
        }

        if (!def.part_number.empty() &&
            !editor_actions.setNodePartNumber(result->graphNodeId(), def.part_number))
            throw std::logic_error("failed to set component part number");
    } catch (const std::exception &e) {
        // A definition that passes field validation can still trip an engine
        // deserialize() on an engine-state key the library schema does not
        // describe (e.g. "enable_nonlinear" as a string).
        LOG_WARN("ComponentLibrary: failed to instantiate '%s' (%s): %s", def.part_number.c_str(),
                 def.type.c_str(), e.what());
        try {
            runtime.removeComponent(result->graphNodeId());
        } catch (...) {
            // Removal failure must not mask the rollback outcome.
        }
        return nullptr;
    }
    return result;
}
