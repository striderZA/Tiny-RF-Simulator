#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>


class ComponentRegistry;
class NodeGraphEngine;
class IComponentEngine;
struct ComponentDefinition {
    int schema_version;
    std::string type;           // "amplifier"
    std::string part_number;    // "AM1143"
    std::string manufacturer;
    std::string description;
    nlohmann::json parameters;
    nlohmann::json test_conditions;
    std::string notes;
    std::string source_path;    // filesystem path for diagnostics
};

class ComponentLibrary {
public:
    void loadFile(const std::string& filepath);
    std::vector<const ComponentDefinition*> all() const;
    void scan(const std::string& directory);
    std::vector<const ComponentDefinition*> byType(const std::string& type) const;
    IComponentEngine* instantiate(const ComponentDefinition& def,
                                  int id,
                                  ComponentRegistry& registry,
                                  NodeGraphEngine& graph);

private:
    std::vector<ComponentDefinition> m_definitions;
};
