#pragma once

#include "component_registry.h"
#include <cstdint>
#include <functional>
#include <optional>

using ComponentFactory =
    std::function<IComponentEngine *(ComponentRegistry &, NodeGraphEngine &, int)>;

class GraphEditorActions;

class CircuitRuntime {
  public:
    CircuitRuntime();

    IComponentEngine *createComponent(const ComponentFactory &factory);
    // Builds a component under an explicit component id and leaves the id counter alone. Only an
    // in-place replacement uses it, while the component that held `component_id` is still
    // registered; that predecessor must be removed before the command returns.
    IComponentEngine *createComponentWithId(const ComponentFactory &factory, int component_id);
    bool removeComponent(int graph_node_id);
    std::optional<int> connect(int start_pin_id, int end_pin_id);
    bool disconnect(int link_id);
    void update(double dt);
    void clearComponentsAndResetIds();
    std::uint64_t epoch() const;

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
    std::uint64_t m_epoch = 0;
};
