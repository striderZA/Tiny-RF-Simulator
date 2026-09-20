#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

class ComponentRegistry;
class NodeGraphEngine;
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

class ComponentLibrary {
  public:
    void loadFile(const std::string &filepath);
    std::vector<const ComponentDefinition *> all() const;
    void scan(const std::string &directory);
    std::vector<const ComponentDefinition *> byType(const std::string &type) const;
    IComponentEngine *instantiate(const ComponentDefinition &def, int id,
                                  ComponentRegistry &registry, NodeGraphEngine &graph);
    std::vector<ValidationIssue> validate(const std::string &type,
                                          const nlohmann::json &parameters) const;
    void upsert(const ComponentDefinition &def);

  private:
    std::vector<ComponentDefinition> m_definitions;
};
