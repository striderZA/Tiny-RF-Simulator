#pragma once

#include "s_parameter_data.h"
#include <string>

#include "component_engine_base.h"
#include "signal_node.h"

class SplitterEngine : public ComponentEngineBase {
  public:
    SplitterEngine(int id, NodeGraphEngine &graph);
    std::string_view type_name() const override { return "splitter"; }
    std::string hoverSummary() const override;
    int outputPinId(int index) const override;
    int outputPinId() const override { return outputPinId(0); }
    int numOutputPins() const override { return 2; }

    void update(double dt) override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json &) override;

    void setSParamMode(bool enabled);
    bool sparamMode() const { return m_sparam_mode; }
    void setSParamFilepath(const std::string &path);
    bool sparamLoaded() const { return m_sparam_data.loaded(); }
    const std::string &sparamFilepath() const { return m_sparam_filepath; }
    const SParameterData &sparamData() const { return m_sparam_data; }

  private:
    bool m_sparam_mode = false;
    std::string m_sparam_filepath;
    SParameterData m_sparam_data;

    static constexpr double SPLIT_LOSS_DB = 3.010299956639812;
};
