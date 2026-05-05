#pragma once

#include "common.h"
#include "node_graph_engine.h"
#include "signal_node.h"
#include "s_parameter_data.h"
#include <string>

class SParameterFilterEngine {
  public:
    SParameterFilterEngine(int id, NodeGraphEngine& graph, const std::string& filepath);
    int id() const { return m_id; }
    int graphNodeId() const { return m_graph_node_id; }
    std::string hoverSummary() const;
    int inputPinId() const;
    int outputPinId() const;

    void update(double dt);
    void reload(const std::string& filepath);

    SignalNode& node() { return m_node; }
    const std::string& filepath() const { return m_filepath; }
    bool loaded() const { return m_data.loaded(); }

    const SParameterData& data() const { return m_data; }

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;
    SignalNode m_node;
    SParameterData m_data;
    std::string m_filepath;
    bool m_dirty = true;
    uint64_t m_cached_input_generation = 0;
};