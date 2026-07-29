#pragma once

#include <nlohmann/json.hpp>
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
