#pragma once
#include "common.h"
#include "node_graph_engine.h"
#include "signal_node.h"

enum class FilterType { LPF, HPF, BPF, BSF };

class IdealFilterEngine {
  public:
    IdealFilterEngine(int id, NodeGraphEngine& graph);

    int id() const { return m_id; }
    int graphNodeId() const { return m_graph_node_id; }
    std::string hoverSummary() const;
    int inputPinId() const;
    int outputPinId() const;

    void setFilterType(FilterType type) { m_type = type; m_dirty = true; }
    FilterType filterType() const { return m_type; }

    void setCutoff_Hz(double fc_Hz) { m_fc_low_Hz = fc_Hz; m_fc_high_Hz = fc_Hz; m_dirty = true; }
    void setCutoffs_Hz(double low, double high) { m_fc_low_Hz = low; m_fc_high_Hz = high; m_dirty = true; }
    double fcLow_Hz() const { return m_fc_low_Hz; }
    double fcHigh_Hz() const { return m_fc_high_Hz; }

    SignalNode& node() { return m_node; }
    void update(double dt);

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;
    SignalNode m_node;
    bool m_dirty = true;
    FilterType m_type = FilterType::LPF;
    double m_fc_low_Hz = 100e6;
    double m_fc_high_Hz = 200e6;
    const Spectrum* m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;

    bool isInPassband(double freq_Hz) const;
};
