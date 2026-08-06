#pragma once

#include <algorithm>

#include "component_interface.h"
#include "node_graph_engine.h"
#include "signal_node.h"

class AdcEngine : public IComponentEngine {
  public:
    AdcEngine(int id, NodeGraphEngine &graph);

    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_node_id; }
    std::string_view type_name() const override { return "adc"; }
    std::string hoverSummary() const override;
    int inputPinId() const override;
    int outputPinId() const override;

    void update(double dt) override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json &) override;

    double fs_Hz() const { return m_fs_Hz; }
    void setFs_Hz(double fs) {
        double clamped = std::max(fs, 1.0);
        if (clamped != m_fs_Hz) {
            m_fs_Hz = clamped;
            m_dirty = true;
        }
    }
    double nsd_dBm_per_Hz() const { return m_nsd_dBm_per_Hz; }
    void setNsd_dBm_per_Hz(double nsd) {
        if (nsd != m_nsd_dBm_per_Hz) {
            m_nsd_dBm_per_Hz = nsd;
            m_dirty = true;
        }
    }

    SignalNode &node() override { return m_node; }
    const SignalNode &node() const override { return m_node; }

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine *m_graph;
    SignalNode m_node;

    double m_fs_Hz = 1e9;
    double m_nsd_dBm_per_Hz = -155.0;
    bool m_dirty = true;
    const Spectrum *m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;
};
