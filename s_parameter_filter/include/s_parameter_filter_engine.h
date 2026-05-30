#pragma once

#include "common.h"
#include "node_graph_engine.h"
#include "signal_node.h"
#include "s_parameter_data.h"
#include <string>
#include "component_interface.h"

class SParameterFilterEngine : public IComponentEngine {
  public:
    SParameterFilterEngine(int id, NodeGraphEngine& graph, const std::string& filepath);
    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_node_id; }
    std::string hoverSummary() const override;
    int inputPinId() const override;
    int outputPinId() const override;

    void update(double dt) override;
    void reload(const std::string& filepath);

    SignalNode& node() override { return m_node; }
    const SignalNode& node() const override { return m_node; }
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
    const Spectrum* m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;
};