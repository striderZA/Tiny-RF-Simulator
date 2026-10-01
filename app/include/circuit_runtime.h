#pragma once

#include "component_registry.h"
#include <functional>
#include <optional>

using ComponentFactory =
    std::function<IComponentEngine *(ComponentRegistry &, NodeGraphEngine &, int)>;

class GraphEditorActions;

class CircuitRuntime {
  public:
    CircuitRuntime();

    IComponentEngine *createComponent(const ComponentFactory &factory);
    bool removeComponent(int graph_node_id);
    std::optional<int> connect(int start_pin_id, int end_pin_id);
    bool disconnect(int link_id);
    void update(double dt);
    void clearComponentsAndResetIds();

    const NodeGraphEngine &graph() const;
    const ComponentRegistry &components() const;
    const ViewManager &viewManager() const;
    int nextComponentId() const;
    void setNextComponentId(int id);

  private:
    friend class GraphEditorActions;

    NodeGraphEngine m_graph;
    ViewManager m_view_manager;
    ComponentRegistry m_components;
    int m_next_component_id = 100;
};
