#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class CircuitRuntime;
class GraphEditorActions;
class IComponentEngine;

struct DataFileRef {
    std::string type; // "s_parameters"
    std::string path; // relative or absolute path
};

struct ValidationIssue {
    std::string field; // empty = whole-definition issue (e.g. unknown type)
    std::string message;
};

// --- Library entry / data-file naming ---------------------------------------
// Keep only filesystem-safe characters for one path segment: [A-Za-z0-9-_ ],
// trimmed of surrounding spaces. Every other character (including separators
// and '.') is dropped, so the result can never traverse out of the directory
// it is joined to; `fallback` is returned when nothing survives. Used for the
// library JSON/directory names and for the name of a copied data file.
std::string sanitizePathSegment(const std::string &s, const std::string &fallback);

// Destination for a data file being copied into a library entry's directory:
// `dest_dir / name`, or nullopt when `name` is not a bare file name (the caller
// must then refuse the save instead of joining an escaping name). The
// component-authoring save path uses this for the picked S-param file
// (issue #120).
std::optional<std::filesystem::path> dataFileCopyDestination(const std::string &dest_dir,
                                                             const std::string &name);

struct ComponentDefinition {
    int schema_version;
    std::string type;        // "amplifier"
    std::string part_number; // "AM1143"
    std::string manufacturer;
    std::string description;
    nlohmann::json parameters;
    nlohmann::json test_conditions;
    std::string notes;
    std::string source_path; // filesystem path for diagnostics
    std::vector<DataFileRef> data_files;
    std::vector<ValidationIssue> issues;
};

enum class ComponentLibraryCreateStatus { Created, InvalidDefinition, AlreadyExists, IoError };

struct ComponentLibraryCreateResult {
    ComponentLibraryCreateStatus status = ComponentLibraryCreateStatus::IoError;
    std::filesystem::path path;
    std::vector<ValidationIssue> issues;
    std::string message;
};

// Containment-checked absolute path of the first data_files entry of `data_type`, resolved
// against the directory of definition.source_path. Returns nullopt when source_path is empty,
// no entry matches, or the entry escapes that directory.
std::optional<std::filesystem::path> componentDataFilePath(const ComponentDefinition &definition,
                                                           std::string_view data_type);

class ComponentLibrary {
  public:
    // A data file (or path parameter) that instantiate() could not use. The engine is still
    // returned: the caller decides whether a fallback to the definition's scalar defaults is
    // acceptable (the editor accepts it) or the component is unusable (a budget stage).
    struct DataFileLoadIssue {
        std::string type;   // "s_parameters", or the path-parameter key
        std::string path;   // the reference as written in the definition
        std::string reason; // human-readable
    };

    void loadFile(const std::string &filepath);
    std::vector<const ComponentDefinition *> all() const;
    void scan(const std::string &directory);
    // True when the directory of `source_path` lies within one of the directories passed to
    // scan() (or registered by createUserEntry()). The directory is the base callers resolve
    // a definition's relative data files against, so a source path equal to a root itself is
    // not contained. Empty paths and libraries with no root are false.
    bool containsLibraryPath(std::string_view source_path) const;
    std::vector<const ComponentDefinition *> byType(const std::string &type) const;
    // `component_id` reuses an existing component id (in-place replacement); nullopt reserves
    // a fresh one from the runtime.
    // `data_file_issues`, when set, *appends* one entry per data file or path parameter the
    // instantiation could not use (it is never cleared). The engine is returned regardless;
    // callers that cannot tolerate the scalar-default fallback must inspect the vector.
    IComponentEngine *instantiate(const ComponentDefinition &def, CircuitRuntime &runtime,
                                  GraphEditorActions &editor_actions,
                                  std::optional<int> component_id = std::nullopt,
                                  std::vector<DataFileLoadIssue> *data_file_issues = nullptr);
    std::vector<ValidationIssue> validate(const std::string &type,
                                          const nlohmann::json &parameters) const;
    void upsert(const ComponentDefinition &def);
    // Validates and atomically creates a new JSON-only user-library entry, then indexes it live.
    ComponentLibraryCreateResult createUserEntry(ComponentDefinition definition,
                                                 const std::filesystem::path &library_root);

  private:
    std::vector<ComponentDefinition> m_definitions;
    // Canonical absolute directory paths that index definitions: one entry per distinct
    // directory passed to scan() or created into by createUserEntry().
    // containsLibraryPath() confines a stage's source file to these roots.
    std::vector<std::filesystem::path> m_roots;
};
