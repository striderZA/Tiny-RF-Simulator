#pragma once
#include "common.h"
#include "component_interface.h"
#include "node_graph_engine.h"
#include "signal_node.h"

class EqualizerEngine : public IComponentEngine {
  public:
    EqualizerEngine(int id, NodeGraphEngine& graph);

    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_node_id; }
    int inputPinId() const override;
    int outputPinId() const override;
    std::string hoverSummary() const override;
    SignalNode& node() override { return m_node; }
    const SignalNode& node() const override { return m_node; }
    void update(double dt) override;

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;
    SignalNode m_node;                        // 1 input, 1 output
    bool m_dirty = true;
    const Spectrum* m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;
};
