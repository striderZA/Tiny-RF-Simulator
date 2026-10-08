#pragma once

#include "circuit_runtime.h"
#include "component_params.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class GraphEditorActions;
class ComponentLibrary;
struct ComponentDefinition;

struct ComponentAddResult {
    IComponentEngine *component = nullptr;
    ParamWriteResult params;
};

// EditorCommands — the single entry point for user-level project edits.
//
// Each command performs its runtime/editor mutation and then every app-level
// side effect that edit implies, in one place: derived group boundaries are
// rebuilt after topology changes, component-bound views are re-synchronized
// after the component set changes (before the command returns, so no view can
// outlive its engine), and the project revision advances. A rejected command
// changes nothing, including the revision.
//
// Dirty state is derived, not flagged: the project is dirty while its revision
// differs from the revision recorded by the last markClean() (a successful
// save, load, or New). Edits that do not go through a command here — widget
// parameter edits, node moves, instrument state — call markModified().
//
// UI-free: it depends only on CircuitRuntime and GraphEditorActions, and
// reaches UI-owned views through onComponentsChanged.
class EditorCommands {
  public:
    EditorCommands(CircuitRuntime &runtime, GraphEditorActions &editor_actions);

    // Runs after every change to the set of live components.
    std::function<void()> onComponentsChanged;

    IComponentEngine *createComponent(const ComponentFactory &factory);
    ComponentAddResult createComponentWithParams(const ComponentFactory &factory,
                                                 const nlohmann::ordered_json &params);
    ComponentAddResult
    addLibraryPart(ComponentLibrary &library, const ComponentDefinition &definition,
                   const nlohmann::ordered_json &params = nlohmann::ordered_json::object());
    ParamWriteResult setComponentParams(int graph_node_id, const nlohmann::ordered_json &params);
    bool removeComponent(int graph_node_id);
    // Adopts components that were created directly through the runtime (e.g.
    // ComponentLibrary::instantiate()): syncs views and records the edit.
    void componentsAdded();

    std::optional<int> connect(int start_pin_id, int end_pin_id);
    bool disconnect(int link_id);

    // Probe edits do not advance the revision (pre-existing semantics).
    bool addProbePin(int pin_id);
    bool removeProbePin(int pin_id);

    int createGroup(std::string name, std::vector<int> member_node_ids);
    bool removeGroup(int group_id);
    bool renameGroup(int group_id, std::string name);
    bool setGroupCollapsed(int group_id, bool collapsed);
    // Selection is not project state: it never advances the revision.
    void selectGroup(int group_id);

    void markModified();
    void markClean();
    bool isDirty() const { return m_revision != m_clean_revision; }
    std::uint64_t revision() const { return m_revision; }

  private:
    void componentsChanged();

    CircuitRuntime &m_runtime;
    GraphEditorActions &m_editor_actions;
    std::uint64_t m_revision = 0;
    std::uint64_t m_clean_revision = 0;
};
