#pragma once

#include "component_engine_base.h"
#include "node_graph_engine.h"
#include "s_parameter_data.h"
#include "signal_node.h"
#include "spectrum.h"
#include <memory>
#include <string>

class AttenuatorEngine : public ComponentEngineBase {
  public:
    AttenuatorEngine(int id, NodeGraphEngine &graph);

    std::string_view type_name() const override { return "attenuator"; }
    std::string hoverSummary() const override;
    int outputPinId() const override { return outputPinId(0); }
    int outputPinId(int index) const override;
    void update(double dt) override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json &) override;

    void setAttenuation(double dB);
    double attenuation() const { return m_atten_dB; }

    void setSParamMode(bool enabled);
    bool sparamMode() const { return m_sparam_mode; }

    void setSParamFilepath(const std::string &path);
    bool sparamLoaded() const { return m_sparam_data.loaded(); }
    const std::string &sparamFilepath() const { return m_sparam_filepath; }
    const SParameterData &sparamData() const { return m_sparam_data; }

  private:
    double m_atten_dB = 0.0;

    bool m_sparam_mode = false;
    std::string m_sparam_filepath;
    SParameterData m_sparam_data;
};
