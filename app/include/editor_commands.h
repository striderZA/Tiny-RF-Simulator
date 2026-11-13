#pragma once

#include "circuit_runtime.h"
#include "component_params.h"
#include "editor_history.h"
#include "node_layout.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
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

// Outcome of EditorCommands::swapLibraryPart(). On refusal `component` is null, `error` says why,
// and the circuit is unchanged.
struct ComponentSwapResult {
    IComponentEngine *component = nullptr;
    std::string error;
    int old_graph_node_id = -1;
    int new_graph_node_id = -1;
    // Predecessor pin id -> successor pin id for each port index both parts have.
    std::map<int, int> pin_map;
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

    // Editor-space node positions for moveNodes(). The app wires them to the widget, which owns
    // the imnodes positions; both must be set before a layout command runs.
    std::function<LayoutPoint(int node_id)> readNodePosition;
    std::function<void(int node_id, LayoutPoint position)> writeNodePosition;

    // Grid-space node positions for undo snapshots (pan-independent). The app wires them to the
    // widget.
    std::function<LayoutPoint(int node_id)> readNodeGridPosition;
    std::function<void(int node_id, LayoutPoint grid_position)> writeNodeGridPosition;
    // App-owned undoable values (Task 5 keys: "receiver_requirements", "rf_budget", "na_points").
    std::function<std::map<std::string, nlohmann::json>()> captureAppState;
    std::function<void(const std::string &key, const nlohmann::json &value)> applyAppState;

    // Records the change since the last commit as one undo record. Pushes only when the snapshot
    // differs from the baseline; inside an edit group every commit defers to the outermost group.
    // Returns true when a record was pushed.
    bool commitEdit(const std::string &label);
    // Nests: commands that commit inside a group are recorded by the outermost endEditGroup().
    void beginEditGroup(std::string label);
    // At depth 0: commits with `label`, or the outermost group's label when it is absent. Returns
    // true when a record was pushed.
    bool endEditGroup(std::optional<std::string> label = std::nullopt);

    std::size_t undoCount() const { return m_history.undoCount(); }
    // False when nothing is undoable or an edit group is open.
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    // Empty when the direction is unavailable.
    std::string undoLabel() const;
    std::string redoLabel() const;
    // Drops the records and group depth, and recaptures the baseline.
    void clearHistory();

    IComponentEngine *createComponent(const ComponentFactory &factory);
    ComponentAddResult createComponentWithParams(const ComponentFactory &factory,
                                                 const nlohmann::ordered_json &params);
    ComponentAddResult
    addLibraryPart(ComponentLibrary &library, const ComponentDefinition &definition,
                   const nlohmann::ordered_json &params = nlohmann::ordered_json::object());
    // In-place replacement of a placed component by another library part (issue #200). The
    // successor takes the predecessor's component id, keeps each link whose port it has, and
    // carries groups and probes across. `instrument_pins` lists output pins that app-level
    // instruments select. Nothing changes when a link, probe, or instrument pin sits on a port
    // the new part lacks, or when the physical link policy forbids a link.
    ComponentSwapResult swapLibraryPart(ComponentLibrary &library,
                                        const ComponentDefinition &definition, int graph_node_id,
                                        const std::vector<int> &instrument_pins = {});
    ParamWriteResult setComponentParams(int graph_node_id, const nlohmann::ordered_json &params);
    bool removeComponent(int graph_node_id);
    // Adopts components that were created directly through the runtime (e.g.
    // ComponentLibrary::instantiate()): syncs views and records the edit.
    void componentsAdded();

    std::optional<int> connect(int start_pin_id, int end_pin_id);
    bool disconnect(int link_id);

    // An accepted probe add or remove advances the revision; a refused one does not.
    bool addProbePin(int pin_id);
    bool removeProbePin(int pin_id);

    int createGroup(std::string name, std::vector<int> member_node_ids);
    bool removeGroup(int group_id);
    bool renameGroup(int group_id, std::string name);
    bool setGroupCollapsed(int group_id, bool collapsed);
    // Selection is not project state: it never advances the revision.
    void selectGroup(int group_id);

    // Layout (align and distribute). Places each listed node at its position as one command.
    // Returns the refusal reason, or an empty string when accepted. A list that changes nothing is
    // accepted without advancing the revision. Refused, with nothing changed, when a listed node is
    // unknown or belongs to a collapsed subcircuit.
    std::string moveNodes(const std::vector<LayoutMove> &moves);
    // Task 4 forwarders so the app keeps building until Task 5 removes them with their callers.
    bool undoLayout() { return undo(); }
    bool canUndoLayout() const { return canUndo(); }
    void clearLayoutUndo() { clearHistory(); }

    void markModified();
    void markClean();
    bool isDirty() const { return m_revision != m_clean_revision; }
    std::uint64_t revision() const { return m_revision; }

  private:
    void componentsChanged();
    EditorSnapshot capture() const;
    // Grid positions only, for the position baseline refresh below.
    std::map<int, LayoutPoint> capturePositions() const;
    // Copies the current grid positions into the baseline at depth 0, so a node placed outside a
    // command is not rolled back by the next record. A no-op inside an open edit group, whose
    // baseline must still show where the group started.
    void refreshPositionBaseline();
    // Applies `forward` against one capture taken at its start; every changed key applies only
    // where the current value equals the record's before side. Repairs ids, links, probes, groups,
    // positions, and app state, then syncs views and recaptures the baseline.
    void apply(const EditorDelta &forward);
    bool commitNow(const std::string &label);
    ComponentAddResult finishComponentAdd(IComponentEngine *component,
                                          const nlohmann::ordered_json &params, std::string label);
    std::string addedComponentLabel() const;

    CircuitRuntime &m_runtime;
    GraphEditorActions &m_editor_actions;
    std::uint64_t m_revision = 0;
    std::uint64_t m_clean_revision = 0;
    EditorSnapshot m_baseline;
    EditorHistory m_history;
    // The outermost open group's label; commits are deferred while m_group_depth is above zero.
    std::string m_group_label;
    int m_group_depth = 0;
};
