#include "editor_commands.h"
#include "component_library.h"
#include "graph_editor_actions.h"
#include "graph_link_policy.h"
#include "logging_core.h"
#include <algorithm>
#include <exception>
#include <set>
#include <string>
#include <utility>

namespace {

ParamWriteResult applyParamsToNewComponent(IComponentEngine &component,
                                           const nlohmann::ordered_json &params) {
    static const std::vector<ParameterField> no_state_fields;
    const auto *descriptor = ComponentTypeRegistry::instance().find(component.type_name());
    const auto &state_fields = descriptor ? descriptor->state_fields : no_state_fields;
    return applyComponentParams(component, state_fields, params);
}

const GraphNode *findGraphNode(const NodeGraphEngine &graph, int node_id) {
    for (const GraphNode &node : graph.nodes()) {
        if (node.node_id == node_id)
            return &node;
    }
    return nullptr;
}

std::string nodeLabel(const NodeGraphEngine &graph, int node_id) {
    const GraphNode *node = findGraphNode(graph, node_id);
    return node ? node->label : std::string("component");
}

} // namespace

EditorCommands::EditorCommands(CircuitRuntime &runtime, GraphEditorActions &editor_actions)
    : m_runtime(runtime), m_editor_actions(editor_actions) {
    m_baseline = capture();
}

IComponentEngine *EditorCommands::createComponent(const ComponentFactory &factory) {
    refreshPositionBaseline();
    IComponentEngine *component = m_runtime.createComponent(factory);
    if (!component)
        return nullptr;
    componentsChanged();
    markModified();
    commitEdit("Add " + nodeLabel(m_runtime.graph(), component->graphNodeId()));
    return component;
}

ComponentAddResult EditorCommands::finishComponentAdd(IComponentEngine *component,
                                                      const nlohmann::ordered_json &params,
                                                      std::string label) {
    ComponentAddResult result;
    if (!component)
        return result;

    result.params = applyParamsToNewComponent(*component, params);
    if (!result.params.ok()) {
        (void)m_runtime.removeComponent(component->graphNodeId());
        return result;
    }

    result.component = component;
    componentsChanged();
    markModified();
    commitEdit(label);
    return result;
}

ComponentAddResult EditorCommands::createComponentWithParams(const ComponentFactory &factory,
                                                             const nlohmann::ordered_json &params) {
    refreshPositionBaseline();
    IComponentEngine *component = m_runtime.createComponent(factory);
    return finishComponentAdd(
        component, params,
        "Add " + nodeLabel(m_runtime.graph(), component ? component->graphNodeId() : -1));
}

ComponentAddResult EditorCommands::addLibraryPart(ComponentLibrary &library,
                                                  const ComponentDefinition &definition,
                                                  const nlohmann::ordered_json &params) {
    refreshPositionBaseline();
    return finishComponentAdd(library.instantiate(definition, m_runtime, m_editor_actions), params,
                              "Add " + definition.part_number);
}

ComponentSwapResult EditorCommands::swapLibraryPart(ComponentLibrary &library,
                                                    const ComponentDefinition &definition,
                                                    int graph_node_id,
                                                    const std::vector<int> &instrument_pins) {
    refreshPositionBaseline();
    ComponentSwapResult result;
    const IComponentEngine *predecessor = m_runtime.components().find(graph_node_id);
    const GraphNode *node = findGraphNode(m_runtime.graph(), graph_node_id);
    if (!predecessor || !node) {
        result.error = "The component is no longer in the circuit.";
        return result;
    }

    // Copy what the swap reads from the graph: instantiating the successor appends to the node
    // vector, which invalidates `node`.
    const int component_id = predecessor->id();
    const std::string old_label = node->label;
    const std::vector<int> old_inputs = node->input_pin_ids;
    const std::vector<int> old_outputs = node->output_pin_ids;
    const auto owns = [&](int pin) {
        return std::find(old_inputs.begin(), old_inputs.end(), pin) != old_inputs.end() ||
               std::find(old_outputs.begin(), old_outputs.end(), pin) != old_outputs.end();
    };
    const auto describe = [&](int pin) -> std::string {
        for (size_t i = 0; i < old_inputs.size(); ++i) {
            if (old_inputs[i] == pin)
                return "input " + std::to_string(i + 1);
        }
        for (size_t i = 0; i < old_outputs.size(); ++i) {
            if (old_outputs[i] == pin)
                return "output " + std::to_string(i + 1);
        }
        return "a port";
    };
    std::vector<GraphLink> old_links;
    for (const GraphLink &link : m_runtime.graph().links()) {
        if (owns(link.start_pin_id) || owns(link.end_pin_id))
            old_links.push_back(link);
    }

    IComponentEngine *successor =
        library.instantiate(definition, m_runtime, m_editor_actions, component_id);
    const std::string part = definition.part_number;
    if (!successor) {
        result.error = "Could not create " + part + "; see the log for details.";
        return result;
    }
    const int successor_node_id = successor->graphNodeId();
    const auto refuse = [&](std::string reason) {
        // Nothing is linked or synced yet, so discarding the successor restores the circuit.
        (void)m_runtime.removeComponent(successor_node_id);
        result.error = std::move(reason);
        return result;
    };
    const GraphNode *created = findGraphNode(m_runtime.graph(), successor_node_id);
    if (!created)
        return refuse("Could not create " + part + "; see the log for details.");

    // Ports pair by index; a port the successor lacks has no counterpart in pin_map.
    std::map<int, int> pin_map;
    for (size_t i = 0; i < std::min(old_inputs.size(), created->input_pin_ids.size()); ++i)
        pin_map[old_inputs[i]] = created->input_pin_ids[i];
    for (size_t i = 0; i < std::min(old_outputs.size(), created->output_pin_ids.size()); ++i)
        pin_map[old_outputs[i]] = created->output_pin_ids[i];

    for (const GraphLink &link : old_links) {
        for (const int pin : {link.start_pin_id, link.end_pin_id}) {
            if (owns(pin) && pin_map.count(pin) == 0)
                return refuse("Cannot change to " + part + ": " + describe(pin) +
                              " is connected, and " + part + " has no such port.");
        }
    }
    for (const int pin : m_runtime.graph().probePins()) {
        if (owns(pin) && pin_map.count(pin) == 0)
            return refuse("Cannot change to " + part + ": " + describe(pin) + " is probed, and " +
                          part + " has no such port.");
    }
    for (const int pin : instrument_pins) {
        if (owns(pin) && pin_map.count(pin) == 0)
            return refuse("Cannot change to " + part + ": " + describe(pin) +
                          " is selected by an instrument, and " + part + " has no such port.");
    }

    const auto mapped = [&pin_map](int pin) {
        const auto it = pin_map.find(pin);
        return it == pin_map.end() ? pin : it->second;
    };
    const auto componentForPin = [this](int pin) -> const IComponentEngine * {
        const int owner = m_runtime.graph().nodeIdForPin(pin);
        return owner < 0 ? nullptr : m_runtime.components().find(owner);
    };
    struct PlannedLink {
        int start_pin;
        int end_pin;
    };
    std::vector<PlannedLink> planned;
    planned.reserve(old_links.size());
    for (const GraphLink &link : old_links) {
        const int start = mapped(link.start_pin_id);
        const int end = mapped(link.end_pin_id);
        if (!graphLinkAllowed(componentForPin(start), componentForPin(end), start, end))
            return refuse("Cannot change to " + part +
                          ": the physical link policy does not allow one of its connections.");
        planned.push_back({start, end});
    }

    // The successor's pins are fresh, but downstream inputs still hold the predecessor's links,
    // so detach every old link before attaching the successor's.
    for (const GraphLink &link : old_links)
        (void)m_runtime.disconnect(link.link_id);
    std::vector<int> attached;
    for (const PlannedLink &link : planned) {
        if (const auto link_id = m_runtime.connect(link.start_pin, link.end_pin)) {
            attached.push_back(*link_id);
            continue;
        }
        // The checks above make this unreachable. Restore the predecessor's wiring rather than
        // leave the circuit half-rewired.
        for (const int id : attached)
            (void)m_runtime.disconnect(id);
        for (const GraphLink &old : old_links)
            (void)m_runtime.connect(old.start_pin_id, old.end_pin_id);
        return refuse("Cannot change to " + part + ": a connection was rejected.");
    }

    // Re-point probes and group membership before retiring the predecessor: removing a node drops
    // every probe and group membership still referring to its pins.
    m_editor_actions.replaceNodeReferences(graph_node_id, successor_node_id, pin_map);
    (void)m_runtime.removeComponent(graph_node_id);
    componentsChanged();
    m_editor_actions.topologyChanged();
    markModified();
    commitEdit("Change " + old_label + " to " + part);

    result.component = successor;
    result.old_graph_node_id = graph_node_id;
    result.new_graph_node_id = successor_node_id;
    result.pin_map = std::move(pin_map);
    return result;
}

ParamWriteResult EditorCommands::setComponentParams(int graph_node_id,
                                                    const nlohmann::ordered_json &params) {
    refreshPositionBaseline();
    IComponentEngine *engine = m_runtime.components().find(graph_node_id);
    if (!engine) {
        ParamWriteResult result;
        result.status = ParamWriteStatus::UnknownComponent;
        return result;
    }

    static const std::vector<ParameterField> no_state_fields;
    const auto *descriptor = ComponentTypeRegistry::instance().find(engine->type_name());
    const auto &state_fields = descriptor ? descriptor->state_fields : no_state_fields;
    ParamWriteResult result = applyComponentParams(*engine, state_fields, params);
    if (result.status == ParamWriteStatus::Applied) {
        markModified();
        commitEdit("Edit " + nodeLabel(m_runtime.graph(), graph_node_id));
    }
    return result;
}

bool EditorCommands::removeComponent(int graph_node_id) {
    refreshPositionBaseline();
    // The label is read before the node is gone; the record needs it for its undo label.
    const std::string label = nodeLabel(m_runtime.graph(), graph_node_id);
    // The runtime rewires surviving inputs before returning; views are then
    // re-synchronized before any caller can draw them.
    if (!m_runtime.removeComponent(graph_node_id))
        return false;
    componentsChanged();
    m_editor_actions.topologyChanged();
    markModified();
    commitEdit("Remove " + label);
    return true;
}

void EditorCommands::componentsAdded() {
    refreshPositionBaseline();
    componentsChanged();
    markModified();
    commitEdit("Add " + addedComponentLabel());
}

std::string EditorCommands::addedComponentLabel() const {
    for (const GraphNode &node : m_runtime.graph().nodes()) {
        if (!m_baseline.components.contains(node.node_id))
            return node.label;
    }
    return "component";
}

std::optional<int> EditorCommands::connect(int start_pin_id, int end_pin_id) {
    refreshPositionBaseline();
    const auto link_id = m_runtime.connect(start_pin_id, end_pin_id);
    if (!link_id)
        return std::nullopt;
    m_editor_actions.topologyChanged();
    markModified();
    commitEdit("Connect");
    return link_id;
}

bool EditorCommands::disconnect(int link_id) {
    refreshPositionBaseline();
    if (!m_runtime.disconnect(link_id))
        return false;
    m_editor_actions.topologyChanged();
    markModified();
    commitEdit("Disconnect");
    return true;
}

// Probe add and remove are project edits: they are saved in .rfsim, so an accepted one advances
// the revision and the project reads as unsaved until the next save.
bool EditorCommands::addProbePin(int pin_id) {
    refreshPositionBaseline();
    if (!m_editor_actions.addProbePin(pin_id))
        return false;
    markModified();
    commitEdit("Add probe");
    return true;
}

bool EditorCommands::removeProbePin(int pin_id) {
    refreshPositionBaseline();
    if (!m_editor_actions.removeProbePin(pin_id))
        return false;
    markModified();
    commitEdit("Remove probe");
    return true;
}

int EditorCommands::createGroup(std::string name, std::vector<int> member_node_ids) {
    refreshPositionBaseline();
    const std::string label = name;
    const int group_id = m_editor_actions.createGroup(std::move(name), std::move(member_node_ids));
    if (group_id >= 0) {
        markModified();
        commitEdit("Create group " + label);
    }
    return group_id;
}

bool EditorCommands::removeGroup(int group_id) {
    refreshPositionBaseline();
    const Group *group = m_runtime.graph().groupById(group_id);
    const std::string label = group ? group->name : std::string();
    if (!m_editor_actions.removeGroup(group_id))
        return false;
    markModified();
    commitEdit("Remove group " + label);
    return true;
}

bool EditorCommands::renameGroup(int group_id, std::string name) {
    refreshPositionBaseline();
    if (!m_editor_actions.renameGroup(group_id, std::move(name)))
        return false;
    markModified();
    commitEdit("Rename group");
    return true;
}

bool EditorCommands::setGroupCollapsed(int group_id, bool collapsed) {
    refreshPositionBaseline();
    if (!m_editor_actions.setGroupCollapsed(group_id, collapsed))
        return false;
    markModified();
    commitEdit(collapsed ? "Collapse group" : "Expand group");
    return true;
}

void EditorCommands::selectGroup(int group_id) { m_editor_actions.selectGroup(group_id); }

std::string EditorCommands::moveNodes(const std::vector<LayoutMove> &moves) {
    refreshPositionBaseline();
    // Validate the whole list before touching anything, so a refusal changes nothing.
    const NodeGraphEngine &graph = m_runtime.graph();
    std::set<int> listed;
    for (const LayoutMove &move : moves) {
        if (!listed.insert(move.node_id).second)
            return "The layout lists the same node twice.";
        if (!findGraphNode(graph, move.node_id))
            return "The node is no longer in the circuit.";
        for (int group_id : graph.groupsContainingNode(move.node_id)) {
            if (graph.isGroupCollapsed(group_id))
                return "Cannot align or distribute nodes inside collapsed subcircuit \"" +
                       graph.groupById(group_id)->name + "\". Expand it first.";
        }
    }

    // Only nodes that actually move are written; the commit records exactly those.
    std::vector<LayoutMove> changed;
    for (const LayoutMove &move : moves) {
        const LayoutPoint current = readNodePosition(move.node_id);
        if (current.x == move.position.x && current.y == move.position.y)
            continue;
        changed.push_back(move);
    }
    if (changed.empty())
        return {};

    for (const LayoutMove &move : changed)
        writeNodePosition(move.node_id, move.position);
    markModified();
    commitEdit("Arrange nodes");
    return {};
}

void EditorCommands::markModified() { ++m_revision; }

void EditorCommands::markClean() { m_clean_revision = m_revision; }

void EditorCommands::componentsChanged() {
    if (onComponentsChanged)
        onComponentsChanged();
}

EditorSnapshot EditorCommands::capture() const {
    EditorSnapshot snapshot;
    for (IComponentEngine *component : m_runtime.components().all()) {
        if (!component)
            continue;
        const int node_id = component->graphNodeId();
        ComponentState state;
        state.component_id = component->id();
        state.type_name = std::string(component->type_name());
        state.state = component->serialize();
        if (const GraphNode *node = findGraphNode(m_runtime.graph(), node_id)) {
            state.input_pins = node->input_pin_ids;
            state.output_pins = node->output_pin_ids;
            state.part_number = node->part_number;
        }
        snapshot.components.emplace(node_id, std::move(state));
    }
    for (const GraphLink &link : m_runtime.graph().links())
        snapshot.links.insert(LinkEnds{link.start_pin_id, link.end_pin_id});
    snapshot.probes = m_runtime.graph().probePins();
    for (const Group &group : m_runtime.graph().groups())
        snapshot.groups.emplace(group.id,
                                GroupState{group.name, group.member_node_ids, group.collapsed});
    snapshot.positions = capturePositions();
    if (captureAppState)
        snapshot.app_state = captureAppState();
    return snapshot;
}

std::map<int, LayoutPoint> EditorCommands::capturePositions() const {
    std::map<int, LayoutPoint> positions;
    if (!readNodeGridPosition)
        return positions;
    for (const GraphNode &node : m_runtime.graph().nodes())
        positions.emplace(node.node_id, readNodeGridPosition(node.node_id));
    return positions;
}

void EditorCommands::refreshPositionBaseline() {
    if (m_group_depth > 0 || !readNodeGridPosition)
        return;
    m_baseline.positions = capturePositions();
}

bool EditorCommands::commitNow(const std::string &label) {
    EditorSnapshot after = capture();
    EditorDelta delta = diffSnapshots(m_baseline, after);
    m_baseline = std::move(after);
    if (delta.empty())
        return false;
    m_history.push(HistoryRecord{label, std::move(delta)});
    return true;
}

bool EditorCommands::commitEdit(const std::string &label) {
    // Inside a group the outermost endEditGroup() records the whole group as one delta.
    if (m_group_depth > 0)
        return false;
    return commitNow(label);
}

void EditorCommands::beginEditGroup(std::string label) {
    if (m_group_depth == 0) {
        refreshPositionBaseline();
        m_group_label = std::move(label);
    }
    ++m_group_depth;
}

bool EditorCommands::endEditGroup(std::optional<std::string> label) {
    if (m_group_depth == 0)
        return false;
    --m_group_depth;
    if (m_group_depth > 0)
        return false;
    return commitNow(label.value_or(m_group_label));
}

bool EditorCommands::canUndo() const {
    return m_group_depth == 0 && m_history.nextUndo() != nullptr;
}

bool EditorCommands::canRedo() const {
    return m_group_depth == 0 && m_history.nextRedo() != nullptr;
}

std::string EditorCommands::undoLabel() const {
    const HistoryRecord *record = m_group_depth == 0 ? m_history.nextUndo() : nullptr;
    return record ? record->label : std::string();
}

std::string EditorCommands::redoLabel() const {
    const HistoryRecord *record = m_group_depth == 0 ? m_history.nextRedo() : nullptr;
    return record ? record->label : std::string();
}

bool EditorCommands::undo() {
    // Undoing inside a group would split the record the group is still building.
    if (m_group_depth > 0)
        return false;
    const HistoryRecord *record = m_history.nextUndo();
    if (!record)
        return false;
    apply(reversed(record->delta));
    m_history.stepBack();
    return true;
}

bool EditorCommands::redo() {
    if (m_group_depth > 0)
        return false;
    const HistoryRecord *record = m_history.nextRedo();
    if (!record)
        return false;
    apply(record->delta);
    m_history.stepForward();
    return true;
}

void EditorCommands::clearHistory() {
    m_history.clear();
    m_group_depth = 0;
    m_group_label.clear();
    m_baseline = capture();
}

void EditorCommands::apply(const EditorDelta &forward) {
    // One capture for the whole pass: each key applies only where the current value still equals
    // the record's before side, so a key edited since the record is left alone.
    const EditorSnapshot current = capture();
    const auto samePoint = [](const LayoutPoint &lhs, const LayoutPoint &rhs) {
        return lhs.x == rhs.x && lhs.y == rhs.y;
    };

    // 1. Links the record removed: found by pin pair, since a redo reallocates link ids.
    for (const LinkEnds &ends : forward.links_removed) {
        if (!current.links.contains(ends))
            continue;
        for (const GraphLink &link : m_runtime.graph().links()) {
            if (link.start_pin_id == ends.start_pin && link.end_pin_id == ends.end_pin) {
                (void)m_runtime.disconnect(link.link_id);
                break;
            }
        }
    }

    // 2. Groups the record ended. Removing the last members of a group drops it with them, so this
    // runs before the component removals.
    for (const auto &[group_id, change] : forward.groups) {
        if (change.after || !change.before)
            continue;
        const auto found = current.groups.find(group_id);
        if (found == current.groups.end() || !(found->second == *change.before))
            continue;
        (void)m_editor_actions.removeGroup(group_id);
    }

    // 3. Components the record removed.
    for (const auto &[node_id, change] : forward.components) {
        if (change.after || !change.before)
            continue;
        const auto found = current.components.find(node_id);
        if (found == current.components.end() || !(found->second == *change.before))
            continue;
        (void)m_runtime.removeComponent(node_id);
    }

    // 4. Components the record brings back, under their original component, node, and pin ids.
    for (const auto &[node_id, change] : forward.components) {
        if (!change.after || change.before || current.components.contains(node_id))
            continue;
        const ComponentState &state = *change.after;
        const ComponentTypeDescriptor *descriptor =
            ComponentTypeRegistry::instance().find(state.type_name);
        if (!descriptor) {
            LOG_WARN("Undo skipped component %d: unknown type '%s'", node_id,
                     state.type_name.c_str());
            continue;
        }
        IComponentEngine *component = nullptr;
        try {
            component = m_runtime.restoreComponent(descriptor->create, state.component_id, node_id,
                                                   state.input_pins, state.output_pins);
        } catch (const std::exception &error) {
            LOG_WARN("Undo could not restore component %d: %s", node_id, error.what());
            continue;
        }
        if (!component) {
            LOG_WARN("Undo could not restore component %d: its ids or pins changed", node_id);
            continue;
        }
        try {
            component->deserialize(state.state);
        } catch (const std::exception &error) {
            LOG_WARN("Undo could not restore component %d state: %s", node_id, error.what());
        }
        (void)m_editor_actions.setNodePartNumber(node_id, state.part_number);
    }

    // 5. Changed component state. The runtime rewires nothing here, so links are untouched.
    for (const auto &[node_id, change] : forward.components) {
        if (!change.before || !change.after)
            continue;
        const auto found = current.components.find(node_id);
        if (found == current.components.end() || !(found->second == *change.before))
            continue;
        IComponentEngine *component = m_runtime.components().find(node_id);
        if (!component)
            continue;
        try {
            component->deserialize(change.after->state);
        } catch (const std::exception &error) {
            LOG_WARN("Undo skipped component %d state: %s", node_id, error.what());
        }
    }

    // 6. Links the record added.
    for (const LinkEnds &ends : forward.links_added) {
        if (current.links.contains(ends))
            continue;
        if (!m_runtime.connect(ends.start_pin, ends.end_pin))
            LOG_WARN("Undo could not reconnect pins %d -> %d", ends.start_pin, ends.end_pin);
    }

    // 7. Groups the record brings back, or restores to an earlier membership, name, or collapse.
    for (const auto &[group_id, change] : forward.groups) {
        if (!change.after)
            continue;
        const auto found = current.groups.find(group_id);
        if (!change.before) {
            if (found != current.groups.end())
                continue;
            try {
                if (m_editor_actions.restoreGroup(group_id, *change.after) < 0)
                    LOG_WARN("Undo could not restore group %d", group_id);
            } catch (const std::exception &error) {
                LOG_WARN("Undo could not restore group %d: %s", group_id, error.what());
            }
            continue;
        }
        if (found == current.groups.end() || !(found->second == *change.before))
            continue;
        if (found->second.members != change.after->members) {
            (void)m_editor_actions.removeGroup(group_id);
            try {
                if (m_editor_actions.restoreGroup(group_id, *change.after) < 0)
                    LOG_WARN("Undo could not restore group %d", group_id);
            } catch (const std::exception &error) {
                LOG_WARN("Undo could not restore group %d: %s", group_id, error.what());
            }
            continue;
        }
        if (found->second.name != change.after->name)
            (void)m_editor_actions.renameGroup(group_id, change.after->name);
        if (found->second.collapsed != change.after->collapsed)
            (void)m_editor_actions.setGroupCollapsed(group_id, change.after->collapsed);
    }

    // 8. Probe slots, in recorded order.
    if (forward.probes && forward.probes->before && forward.probes->after &&
        current.probes == *forward.probes->before) {
        m_editor_actions.setProbePins(*forward.probes->after);
    }

    // 9. Grid positions of present nodes.
    if (writeNodeGridPosition) {
        for (const auto &[node_id, change] : forward.positions) {
            if (!change.after || !findGraphNode(m_runtime.graph(), node_id))
                continue;
            const auto found = current.positions.find(node_id);
            if (change.before) {
                if (found == current.positions.end() || !samePoint(found->second, *change.before))
                    continue;
            } else if (found != current.positions.end()) {
                continue;
            }
            writeNodeGridPosition(node_id, *change.after);
        }
    }

    // 10. App-owned values. A value the record removed has no removal hook, so it is left as is.
    if (applyAppState) {
        for (const auto &[key, change] : forward.app_state) {
            if (!change.after)
                continue;
            const auto found = current.app_state.find(key);
            if (change.before) {
                if (found == current.app_state.end() || !(found->second == *change.before))
                    continue;
            } else if (found != current.app_state.end()) {
                continue;
            }
            applyAppState(key, *change.after);
        }
    }

    componentsChanged();
    m_editor_actions.topologyChanged();
    markModified();
    m_baseline = capture();
}
