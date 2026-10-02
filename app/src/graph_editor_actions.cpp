#include "graph_editor_actions.h"
#include <algorithm>
#include <utility>

GraphEditorActions::GraphEditorActions(CircuitRuntime &runtime) : m_runtime(runtime) {}

bool GraphEditorActions::addProbePin(int pin_id) { return m_runtime.m_graph.addProbePin(pin_id); }

bool GraphEditorActions::removeProbePin(int pin_id) {
    return m_runtime.m_graph.removeProbePin(pin_id);
}

void GraphEditorActions::clearProbes() { m_runtime.m_graph.clearProbes(); }

int GraphEditorActions::createGroup(std::string name, std::vector<int> member_node_ids) {
    return m_runtime.m_graph.addGroup(std::move(name), std::move(member_node_ids));
}

bool GraphEditorActions::removeGroup(int group_id) {
    if (!m_runtime.m_graph.groupById(group_id))
        return false;
    m_runtime.m_graph.removeGroup(group_id);
    return true;
}

bool GraphEditorActions::renameGroup(int group_id, std::string name) {
    if (!m_runtime.m_graph.groupById(group_id))
        return false;
    m_runtime.m_graph.renameGroup(group_id, std::move(name));
    return true;
}

bool GraphEditorActions::setGroupCollapsed(int group_id, bool collapsed) {
    if (!m_runtime.m_graph.groupById(group_id))
        return false;
    m_runtime.m_graph.setGroupCollapsed(group_id, collapsed);
    return true;
}

void GraphEditorActions::selectGroup(int group_id) {
    m_runtime.m_graph.setSelectedGroupId(group_id);
}

bool GraphEditorActions::setNodePartNumber(int node_id, std::string part_number) {
    const auto &nodes = m_runtime.m_graph.nodes();
    if (std::none_of(nodes.begin(), nodes.end(),
                     [node_id](const GraphNode &node) { return node.node_id == node_id; }))
        return false;
    m_runtime.m_graph.setNodePartNumber(node_id, part_number);
    return true;
}

void GraphEditorActions::resetForProjectReplacement() {
    m_runtime.m_graph.clearProbes();
    while (!m_runtime.m_graph.groups().empty())
        m_runtime.m_graph.removeGroup(m_runtime.m_graph.groups().back().id);
    m_runtime.m_graph.setSelectedGroupId(-1);
    m_runtime.m_graph.setNextGroupId(50000);
    m_runtime.m_graph.setNextBoundaryPinId(100000);
}

void GraphEditorActions::topologyChanged() {
    for (const auto &group : m_runtime.m_graph.groups())
        m_runtime.m_graph.rebuildGroupBoundaryPins(group.id);
}
