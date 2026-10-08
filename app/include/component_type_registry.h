#pragma once

#include "node_graph_engine.h"
#include <functional>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

class ComponentRegistry;
class NodeGraphEngine;
class IComponentEngine;
class InspectorPanel;

enum class FieldKind { Number, String, Enum, FilePath, Bool };

struct ParameterField {
    std::string key;   // JSON key under "parameters", e.g. "gain_dB"
    std::string label; // UI label, e.g. "Gain"
    std::string unit;  // e.g. "dB", "Hz"; empty if none
    FieldKind kind = FieldKind::Number;
    bool required = false;
    double min = -std::numeric_limits<double>::infinity(); // Number only
    double max = std::numeric_limits<double>::infinity();  // Number only
    std::vector<std::string> enum_values;                  // Enum only
    nlohmann::json default_value;                          // optional
    std::string help;                                      // optional tooltip
    bool read_only = false; // State metadata only; ignored by authoring.
};

struct ComponentTypeDescriptor {
    std::string type;         // canonical key, e.g. "amplifier"
    std::string project_type; // .rfsim save/load name, e.g. "Amplifier"
    std::string display_name; // e.g. "Amplifier"
    std::string menu_label;   // canvas menu item, e.g. "Add Amplifier"
    std::string label_prefix; // graph label prefix, e.g. "Amplifier"
    NodeKind kind = NodeKind::Unknown;
    bool authorable = false; // appears in New Component form combo
    bool supports_sparam_file = false;
    // Apply a containment-checked library S-param file; true means the engine
    // accepted it for S-parameter operation.
    std::function<bool(IComponentEngine &, const std::string &)> load_sparam_file;
    std::vector<ParameterField> fields;
    // Every scalar leaf in an engine's serialize() snapshot, using its persisted
    // key and values (array indices are normalized to []).
    std::vector<ParameterField> state_fields;

    // Create a default engine of this type (no params). Callers apply params
    // via engine->deserialize().
    std::function<IComponentEngine *(ComponentRegistry &, NodeGraphEngine &, int)> create;
    // Inspector property draw. Receives the panel so PFB's multi-instance
    // selector and dirty-flag state stay reachable.
    std::function<void(InspectorPanel &, IComponentEngine &)> draw_inspector;
};

// Normalize concrete array indices in serialize() paths (e.g. tones[12].freq_Hz).
std::string normalizeStatePath(std::string_view path);
// Find state metadata by a serialized key or a path containing concrete array indices.
const ParameterField *findStateField(const ComponentTypeDescriptor &descriptor,
                                     std::string_view path);

class ComponentTypeRegistry {
  public:
    static ComponentTypeRegistry &instance();

    const ComponentTypeDescriptor *find(std::string_view type) const;
    const ComponentTypeDescriptor *findByProjectType(std::string_view name) const;
    std::vector<ComponentTypeDescriptor *> all();

  private:
    ComponentTypeRegistry();
    std::vector<ComponentTypeDescriptor> m_descriptors;
};
