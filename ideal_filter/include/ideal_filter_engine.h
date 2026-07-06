#pragma once
#include "common.h"
#include "node_graph_engine.h"
#include "s_parameter_data.h"
#include "signal_node.h"
#include "component_interface.h"

enum class FilterType { LPF, HPF, BPF, BSF };

class IdealFilterEngine : public IComponentEngine {
  public:
    IdealFilterEngine(int id, NodeGraphEngine& graph);

    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_node_id; }
    std::string hoverSummary() const override;
    int inputPinId() const override;
    int outputPinId() const override;

    void setFilterType(FilterType type) { m_type = type; m_dirty = true; }
    FilterType filterType() const { return m_type; }

    void setCutoff_Hz(double fc_Hz) { m_fc_low_Hz = fc_Hz; m_fc_high_Hz = fc_Hz; m_dirty = true; }
    void setCutoffs_Hz(double low, double high) { m_fc_low_Hz = low; m_fc_high_Hz = high; m_dirty = true; }
    double fcLow_Hz() const { return m_fc_low_Hz; }
    double fcHigh_Hz() const { return m_fc_high_Hz; }

    SignalNode& node() override { return m_node; }
    const SignalNode& node() const override { return m_node; }
    void update(double dt) override;

    // S-parameter mode
    void setSParamFilepath(const std::string& path);
    bool sparamMode() const { return m_sparam_mode; }
    void setSParamMode(bool en) { m_sparam_mode = en; m_dirty = true; }
    bool sparamLoaded() const { return m_sparam_data.loaded(); }
    const std::string& sparamFilepath() const { return m_sparam_filepath; }
    const SParameterData& sparamData() const { return m_sparam_data; }

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

    // S-parameter state
    SParameterData m_sparam_data;
    std::string m_sparam_filepath;
    bool m_sparam_mode = false;
    int m_sparam_fwd_idx = 0;
    const Spectrum* m_cached_sparam_input = nullptr;
    uint64_t m_cached_sparam_generation = 0;
};
