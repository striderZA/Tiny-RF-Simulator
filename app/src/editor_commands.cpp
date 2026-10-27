#include "editor_commands.h"
#include "component_library.h"
#include "graph_editor_actions.h"
#include "graph_link_policy.h"
#include <algorithm>
#include <set>
#include <utility>

namespace {

ParamWriteResult applyParamsToNewComponent(IComponentEngine &component,
                                           const nlohmann::ordered_json &params) {
    static const std::vector<ParameterField> no_state_fields;
    const auto *descriptor = ComponentTypeRegistry::instance().find(component.type_name());
    const auto &state_fields = descriptor ? descriptor->state_fields : no_state_fields;
    return applyComponentParams(component, state_fields, params);
}

ComponentAddResult finishComponentAdd(CircuitRuntime &runtime, EditorCommands &commands,
                                      IComponentEngine *component,
                                      const nlohmann::ordered_json &params) {
    ComponentAddResult result;
    if (!component)
        return result;

    result.params = applyParamsToNewComponent(*component, params);
    if (!result.params.ok()) {
        (void)runtime.removeComponent(component->graphNodeId());
        return result;
    }

    result.component = component;
    commands.componentsAdded();
    return result;
}

const GraphNode *findGraphNode(const NodeGraphEngine &graph, int node_id) {
    for (const GraphNode &node : graph.nodes()) {
        if (node.node_id == node_id)
            return &node;
    }
    return nullptr;
}

} // namespace

EditorCommands::EditorCommands(CircuitRuntime &runtime, GraphEditorActions &editor_actions)
    : m_runtime(runtime), m_editor_actions(editor_actions) {}

IComponentEngine *EditorCommands::createComponent(const ComponentFactory &factory) {
    IComponentEngine *component = m_runtime.createComponent(factory);
    if (!component)
        return nullptr;
    componentsChanged();
    markModified();
    return component;
}

ComponentAddResult EditorCommands::createComponentWithParams(const ComponentFactory &factory,
                                                             const nlohmann::ordered_json &params) {
    return finishComponentAdd(m_runtime, *this, m_runtime.createComponent(factory), params);
}

ComponentAddResult EditorCommands::addLibraryPart(ComponentLibrary &library,
                                                  const ComponentDefinition &definition,
                                                  const nlohmann::ordered_json &params) {
    return finishComponentAdd(m_runtime, *this,
                              library.instantiate(definition, m_runtime, m_editor_actions), params);
}

ComponentSwapResult EditorCommands::swapLibraryPart(ComponentLibrary &library,
                                                    const ComponentDefinition &definition,
                                                    int graph_node_id,
                                                    const std::vector<int> &instrument_pins) {
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

    result.component = successor;
    result.old_graph_node_id = graph_node_id;
    result.new_graph_node_id = successor_node_id;
    result.pin_map = std::move(pin_map);
    return result;
}

ParamWriteResult EditorCommands::setComponentParams(int graph_node_id,
                                                    const nlohmann::ordered_json &params) {
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
    if (result.status == ParamWriteStatus::Applied)
        markModified();
    return result;
}

bool EditorCommands::removeComponent(int graph_node_id) {
    // The runtime rewires surviving inputs before returning; views are then
    // re-synchronized before any caller can draw them.
    if (!m_runtime.removeComponent(graph_node_id))
        return false;
    componentsChanged();
    m_editor_actions.topologyChanged();
    markModified();
    return true;
}

void EditorCommands::componentsAdded() {
    componentsChanged();
    markModified();
}

std::optional<int> EditorCommands::connect(int start_pin_id, int end_pin_id) {
    const auto link_id = m_runtime.connect(start_pin_id, end_pin_id);
    if (!link_id)
        return std::nullopt;
    m_editor_actions.topologyChanged();
    markModified();
    return link_id;
}

bool EditorCommands::disconnect(int link_id) {
    if (!m_runtime.disconnect(link_id))
        return false;
    m_editor_actions.topologyChanged();
    markModified();
    return true;
}

// Probe add and remove are project edits: they are saved in .rfsim, so an accepted one advances
// the revision and the project reads as unsaved until the next save.
bool EditorCommands::addProbePin(int pin_id) {
    if (!m_editor_actions.addProbePin(pin_id))
        return false;
    markModified();
    return true;
}

bool EditorCommands::removeProbePin(int pin_id) {
    if (!m_editor_actions.removeProbePin(pin_id))
        return false;
    markModified();
    return true;
}

int EditorCommands::createGroup(std::string name, std::vector<int> member_node_ids) {
    const int group_id = m_editor_actions.createGroup(std::move(name), std::move(member_node_ids));
    if (group_id >= 0)
        markModified();
    return group_id;
}

bool EditorCommands::removeGroup(int group_id) {
    if (!m_editor_actions.removeGroup(group_id))
        return false;
    markModified();
    return true;
}

bool EditorCommands::renameGroup(int group_id, std::string name) {
    if (!m_editor_actions.renameGroup(group_id, std::move(name)))
        return false;
    markModified();
    return true;
}

bool EditorCommands::setGroupCollapsed(int group_id, bool collapsed) {
    if (!m_editor_actions.setGroupCollapsed(group_id, collapsed))
        return false;
    markModified();
    return true;
}

void EditorCommands::selectGroup(int group_id) { m_editor_actions.selectGroup(group_id); }

std::string EditorCommands::moveNodes(const std::vector<LayoutMove> &moves) {
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

    // Only nodes that actually move are written and recorded for undo.
    std::vector<LayoutMove> replaced;
    std::vector<LayoutMove> changed;
    for (const LayoutMove &move : moves) {
        const LayoutPoint current = readNodePosition(move.node_id);
        if (current.x == move.position.x && current.y == move.position.y)
            continue;
        replaced.push_back({move.node_id, current});
        changed.push_back(move);
    }
    if (changed.empty())
        return {};

    for (const LayoutMove &move : changed)
        writeNodePosition(move.node_id, move.position);
    markModified();
    m_layout_undo = std::move(replaced);
    m_layout_undo_revision = m_revision;
    return {};
}

bool EditorCommands::canUndoLayout() const {
    return !m_layout_undo.empty() && m_layout_undo_revision == m_revision;
}

bool EditorCommands::undoLayout() {
    if (!canUndoLayout())
        return false;
    for (const LayoutMove &move : m_layout_undo) {
        if (!findGraphNode(m_runtime.graph(), move.node_id))
            return false;
    }
    for (const LayoutMove &move : m_layout_undo)
        writeNodePosition(move.node_id, move.position);
    m_layout_undo.clear();
    markModified();
    return true;
}

void EditorCommands::markModified() { ++m_revision; }

void EditorCommands::markClean() { m_clean_revision = m_revision; }

void EditorCommands::componentsChanged() {
    if (onComponentsChanged)
        onComponentsChanged();
}
