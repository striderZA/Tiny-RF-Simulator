#pragma once

#include "component_interface.h"
#include "node_graph_engine.h"
#include "s_parameter_data.h"
#include "signal_node.h"
#include <string>
#include <algorithm>

class EqualizerEngine : public IComponentEngine {
public:
    EqualizerEngine(int id, NodeGraphEngine& graph);

    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_node_id; }
    std::string hoverSummary() const override;
    int inputPinId() const override;
    int outputPinId() const override;
    SignalNode& node() override { return m_node; }
    const SignalNode& node() const override { return m_node; }
    void update(double dt) override;

    // Ideal mode parameters
    void setRefGain_dB(double g) { m_ref_gain_dB = g; m_dirty = true; }
    double refGain_dB() const { return m_ref_gain_dB; }
    void setRefFreq_Hz(double f) { m_ref_freq_Hz = std::max(f, 1.0); m_dirty = true; }
    double refFreq_Hz() const { return m_ref_freq_Hz; }
    void setSlope_dBPerDecade(double s) { m_slope_dB_per_decade = s; m_dirty = true; }
    double slope_dBPerDecade() const { return m_slope_dB_per_decade; }

    // S-param mode
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
    const Spectrum* m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;

    // Ideal mode
    double m_ref_gain_dB = 0.0;
    double m_ref_freq_Hz = 1e9;
    double m_slope_dB_per_decade = 0.0;

    // S-param mode
    SParameterData m_sparam_data;
    std::string m_sparam_filepath;
    bool m_sparam_mode = false;
    int m_sparam_fwd_idx = 0;
    const Spectrum* m_cached_sparam_input = nullptr;
    uint64_t m_cached_sparam_generation = 0;
};
