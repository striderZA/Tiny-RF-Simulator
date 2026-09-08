#include "component_registry.h"
#include "logging_core.h"
#include <algorithm>

ComponentRegistry::ComponentRegistry(NodeGraphEngine &graph, ViewManager &view)
    : m_graph(graph), m_view(view) {}

bool ComponentRegistry::remove(int graphNodeId) {
    for (size_t i = 0; i < m_components.size(); ++i) {
        if (m_components[i]->graphNodeId() == graphNodeId) {
            IComponentEngine *comp = m_components[i].get();
            m_view.unregisterNode(&comp->node());
            m_graph.removeNodeForSignalNode(&comp->node());

            auto &idx = m_type_index[std::type_index(typeid(*comp))];
            idx.erase(std::remove(idx.begin(), idx.end(), static_cast<IComponentEngine *>(comp)),
                      idx.end());

            m_graph_index.erase(graphNodeId);
            m_components.erase(m_components.begin() + static_cast<std::ptrdiff_t>(i));
            rebuildView();
            LOG_INFO("Removed component (graph node %d)", graphNodeId);
            return true;
        }
    }
    LOG_WARN("remove: graph node %d not found", graphNodeId);
    return false;
}

IComponentEngine *ComponentRegistry::find(int graphNodeId) const {
    auto it = m_graph_index.find(graphNodeId);
    return it != m_graph_index.end() ? it->second : nullptr;
}

std::string ComponentRegistry::hoverSummary(int graphNodeId) const {
    auto *comp = find(graphNodeId);
    return comp ? comp->hoverSummary() : "";
}

void ComponentRegistry::rebuildView() {
    std::vector<IComponentEngine *> new_view;
    new_view.reserve(m_components.size());
    for (auto &comp : m_components)
        new_view.push_back(comp.get());
    m_all_view.swap(new_view);
}
