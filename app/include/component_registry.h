#pragma once

#include "component_interface.h"
#include "node_graph_engine.h"
#include "view_manager.h"
#include <memory>
#include <span>
#include <stdexcept>
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
        const int graph_node_id = ptr->graphNodeId();
        const auto type = std::type_index(typeid(T));
        bool view_registered = false;
        bool type_entry_added = false;
        bool type_ptr_added = false;
        bool graph_index_added = false;
        bool component_added = false;
        try {
            m_view.registerNode(&ptr->node());
            view_registered = true;

            auto type_it = m_type_index.find(type);
            if (type_it == m_type_index.end()) {
                type_it = m_type_index.emplace(type, std::vector<IComponentEngine *>{}).first;
                type_entry_added = true;
            }
            type_it->second.push_back(static_cast<IComponentEngine *>(ptr));
            type_ptr_added = true;

            auto [graph_it, inserted] = m_graph_index.emplace(graph_node_id, ptr);
            (void)graph_it;
            if (!inserted)
                throw std::logic_error("duplicate component graph node ID");
            graph_index_added = true;

            m_components.push_back(std::move(comp));
            component_added = true;
            rebuildView();
        } catch (...) {
            if (view_registered)
                m_view.unregisterNode(&ptr->node());
            m_graph.removeNodeForSignalNode(&ptr->node());
            if (component_added)
                m_components.pop_back();
            if (graph_index_added)
                m_graph_index.erase(graph_node_id);
            auto type_it = m_type_index.find(type);
            if (type_ptr_added && type_it != m_type_index.end())
                type_it->second.pop_back();
            if (type_entry_added && type_it != m_type_index.end() && type_it->second.empty())
                m_type_index.erase(type_it);
            throw;
        }
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
    std::unordered_map<int, IComponentEngine *> m_graph_index;
};
