#pragma once
#include "component_engine_base.h"
#include "node_graph_engine.h"
#include "s_parameter_data.h"
#include "signal_node.h"
#include <stdexcept>

enum class FilterType { LPF, HPF, BPF, BSF };

class IdealFilterEngine : public ComponentEngineBase {
  public:
    IdealFilterEngine(int id, NodeGraphEngine &graph);

    std::string_view type_name() const override { return "filter"; }
    std::string hoverSummary() const override;

    void setFilterType(FilterType type) {
        m_type = type;
        m_dirty = true;
    }
    FilterType filterType() const { return m_type; }

    void setCutoff_Hz(double fc_Hz) {
        m_fc_low_Hz = fc_Hz;
        m_fc_high_Hz = fc_Hz;
        m_dirty = true;
    }
    // A band (BPF/BSF) with low > high is rejected with std::invalid_argument and keeps the
    // stored cutoffs. LPF/HPF use only fc_low, so their fc_high is stored unchecked. An equal
    // pair is accepted: LPF -> BPF leaves both at the same value.
    void setCutoffs_Hz(double low, double high) {
        if ((m_type == FilterType::BPF || m_type == FilterType::BSF) && low > high)
            throw std::invalid_argument("ideal filter band: low cutoff exceeds high cutoff");
        m_fc_low_Hz = low;
        m_fc_high_Hz = high;
        m_dirty = true;
    }
    double fcLow_Hz() const { return m_fc_low_Hz; }
    double fcHigh_Hz() const { return m_fc_high_Hz; }

    void update(double dt) override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json &) override;

    // S-parameter mode
    void setSParamFilepath(const std::string &path);
    bool sparamMode() const { return m_sparam_mode; }
    void setSParamMode(bool en) {
        m_sparam_mode = en && (!m_sparam_data.loaded() || m_sparam_data.numPorts() >= 2);
        m_dirty = true;
    }
    bool sparamLoaded() const { return m_sparam_data.loaded(); }
    const std::string &sparamFilepath() const { return m_sparam_filepath; }
    const SParameterData &sparamData() const { return m_sparam_data; }

  private:
    FilterType m_type = FilterType::LPF;
    double m_fc_low_Hz = 100e6;
    double m_fc_high_Hz = 200e6;

    bool isInPassband(double freq_Hz) const;

    // S-parameter state
    SParameterData m_sparam_data;
    std::string m_sparam_filepath;
    bool m_sparam_mode = false;
    int m_sparam_fwd_idx = 0;
    const Spectrum *m_cached_sparam_input = nullptr;
    uint64_t m_cached_sparam_generation = 0;
};
