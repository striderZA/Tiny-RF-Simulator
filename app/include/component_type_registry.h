// app/include/component_type_registry.h
#pragma once

#include <functional>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class ComponentRegistry;
class NodeGraphEngine;
class IComponentEngine;

enum class FieldKind { Number, String, Enum, FilePath, Bool };

struct ParameterField {
    std::string key;   // JSON key under "parameters", e.g. "gain_dB"
    std::string label;  // UI label, e.g. "Gain"
    std::string unit;   // e.g. "dB", "Hz"; empty if none
    FieldKind kind = FieldKind::Number;
    bool required = false;
    double min = -std::numeric_limits<double>::infinity(); // Number only
    double max = std::numeric_limits<double>::infinity();  // Number only
    std::vector<std::string> enum_values;                  // Enum only
    nlohmann::json default_value;                           // optional
    std::string help;                                       // optional tooltip
};

struct ComponentTypeDescriptor {
    std::string type;         // "amplifier"
    std::string display_name; // "Amplifier"
    std::vector<ParameterField> fields;
    bool supports_sparam_file = false;
    std::function<IComponentEngine *(ComponentRegistry &, NodeGraphEngine &, int,
                                      const nlohmann::json &)>
        factory;
};

class ComponentTypeRegistry {
  public:
    static const ComponentTypeRegistry &instance();

    const ComponentTypeDescriptor *find(const std::string &type) const;
    std::vector<const ComponentTypeDescriptor *> all() const;

  private:
    ComponentTypeRegistry();
    std::vector<ComponentTypeDescriptor> m_descriptors;
};
