#include "editor_commands.h"
#include "graph_editor_actions.h"
#include <utility>

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

// Probe edits keep their pre-EditorCommands semantics: they are saved in
// .rfsim but do not, by themselves, mark the project dirty. Whether they
// should is a separate product decision, not part of this command extraction.
bool EditorCommands::addProbePin(int pin_id) { return m_editor_actions.addProbePin(pin_id); }

bool EditorCommands::removeProbePin(int pin_id) { return m_editor_actions.removeProbePin(pin_id); }

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

void EditorCommands::markModified() { ++m_revision; }

void EditorCommands::markClean() { m_clean_revision = m_revision; }

void EditorCommands::componentsChanged() {
    if (onComponentsChanged)
        onComponentsChanged();
}
