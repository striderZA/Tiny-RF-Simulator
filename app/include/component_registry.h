#pragma once

#include "component_interface.h"
#include "node_graph_engine.h"
#include "view_manager.h"
#include <memory>
#include <span>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

class ComponentRegistry {
  public:
    ComponentRegistry(NodeGraphEngine &graph, ViewManager &view);

    template <typename T, typename... Args> T &add(Args &&...args) {
        auto comp = std::make_unique<T>(std::forward<Args>(args)...);
        T *ptr = comp.get();
        m_view.registerNode(&ptr->node());
        m_type_index[std::type_index(typeid(T))].push_back(static_cast<IComponentEngine *>(ptr));
        m_components.push_back(std::move(comp));
        rebuildView();
        return *ptr;
    }

    bool remove(int graphNodeId);
    IComponentEngine *find(int graphNodeId) const;
    std::string hoverSummary(int graphNodeId) const;

    template <typename T> std::vector<T *> byType() const {
        std::vector<T *> result;
        auto it = m_type_index.find(std::type_index(typeid(T)));
        if (it == m_type_index.end())
            return result;
        result.reserve(it->second.size());
        for (auto *p : it->second)
            result.push_back(static_cast<T *>(p));
        return result;
    }

    std::span<IComponentEngine *const> all() const { return m_all_view; }
    size_t size() const { return m_components.size(); }

  private:
    void rebuildView();

    NodeGraphEngine &m_graph;
    ViewManager &m_view;
    std::vector<std::unique_ptr<IComponentEngine>> m_components;
    std::vector<IComponentEngine *> m_all_view;
    std::unordered_map<std::type_index, std::vector<IComponentEngine *>> m_type_index;
};
