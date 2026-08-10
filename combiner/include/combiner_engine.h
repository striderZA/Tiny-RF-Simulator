#pragma once

#include "component_engine_base.h"
#include "node_graph_engine.h"
#include "s_parameter_data.h"
#include "signal_node.h"
#include "spectrum.h"
#include <string>

class CombinerEngine : public ComponentEngineBase {
  public:
    CombinerEngine(int id, NodeGraphEngine &graph);

    std::string_view type_name() const override { return "combiner"; }
    std::string hoverSummary() const override;

    int inputPinId() const override { return inputPinId(0); }
    int inputPinId(int port) const override;
    int outputPinId() const override { return outputPinId(0); }
    int outputPinId(int index) const override;
    int numInputPins() const override { return 2; }
    int numOutputPins() const override { return 1; }

    void update(double dt) override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json &) override;

    void setManualMode(bool enabled);
    bool manualMode() const { return m_manual_mode; }

    void setSParamMode(bool enabled);
    bool sparamMode() const { return m_sparam_mode; }
    void setSParamFilepath(const std::string &path);
    bool sparamLoaded() const { return m_sparam_data.loaded(); }
    const std::string &sparamFilepath() const { return m_sparam_filepath; }
    const SParameterData &sparamData() const { return m_sparam_data; }

  private:
    bool m_manual_mode = true;
    bool m_sparam_mode = false;
    std::string m_sparam_filepath;
    SParameterData m_sparam_data;

    const Spectrum *m_cached_input0_ptr = nullptr;
    const Spectrum *m_cached_input1_ptr = nullptr;
    uint64_t m_cached_input0_generation = 0;
    uint64_t m_cached_input1_generation = 0;

    static constexpr double COMBINER_LOSS_DB = 3.010299956639812;
};
