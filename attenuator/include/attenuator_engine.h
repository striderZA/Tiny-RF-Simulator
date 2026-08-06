#pragma once

#include "component_interface.h"
#include "node_graph_engine.h"
#include "s_parameter_data.h"
#include "signal_node.h"
#include "spectrum.h"
#include <memory>
#include <string>

class AttenuatorEngine : public IComponentEngine {
  public:
    AttenuatorEngine(int id, NodeGraphEngine &graph);

    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_node_id; }
    std::string_view type_name() const override { return "attenuator"; }
    std::string hoverSummary() const override;
    int inputPinId() const override;
    int outputPinId() const override { return outputPinId(0); }
    int outputPinId(int index) const override;
    void update(double dt) override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json &) override;

    SignalNode &node() override { return m_node; }
    const SignalNode &node() const override { return m_node; }

    void setAttenuation(double dB);
    double attenuation() const { return m_atten_dB; }

    void setSParamMode(bool enabled);
    bool sParamMode() const { return m_sparam_mode; }

    void setSParamFile(const std::string &path);
    const std::string &sParamFile() const { return m_sparam_path; }

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine *m_graph = nullptr;

    SignalNode m_node;

    double m_atten_dB = 0.0;

    bool m_sparam_mode = false;
    std::string m_sparam_path;
    SParameterData m_sparam;

    bool m_dirty = true;
    const Spectrum *m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;
};
