#pragma once

#include <string>

#include "component_interface.h"
#include "node_graph_engine.h"
#include "signal_node.h"

class SplitterEngine : public IComponentEngine {
  public:
    SplitterEngine(int id, NodeGraphEngine &graph);
    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_node_id; }
    std::string hoverSummary() const override;
    int inputPinId() const override;
    int outputPinId(int index) const override;
    int outputPinId() const override { return outputPinId(0); }

    void update(double dt) override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json &) override;

    SignalNode &node() override { return m_node; }
    const SignalNode &node() const override { return m_node; }

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine *m_graph = nullptr;

    SignalNode m_node;
    bool m_dirty = true;
    const Spectrum *m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;

    static constexpr double SPLIT_LOSS_DB = 3.010299956639812;
};
