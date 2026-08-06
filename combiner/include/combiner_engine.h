#pragma once

#include "component_interface.h"
#include "node_graph_engine.h"
#include "s_parameter_data.h"
#include "signal_node.h"
#include "spectrum.h"
#include <string>

class CombinerEngine : public IComponentEngine {
  public:
    CombinerEngine(int id, NodeGraphEngine &graph);

    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_node_id; }
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
    SignalNode &node() override { return m_node; }
    const SignalNode &node() const override { return m_node; }

    void setManualMode(bool enabled);
    bool manualMode() const { return m_manual_mode; }

    void setSParamMode(bool enabled);
    bool sParamMode() const { return m_sparam_mode; }
    void setSParamFile(const std::string &path);
    const std::string &sParamFile() const { return m_sparam_path; }

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine *m_graph = nullptr;

    SignalNode m_node;

    bool m_manual_mode = true;
    bool m_sparam_mode = false;
    std::string m_sparam_path;
    SParameterData m_sparam;

    bool m_dirty = true;
    const Spectrum *m_cached_input0_ptr = nullptr;
    const Spectrum *m_cached_input1_ptr = nullptr;
    uint64_t m_cached_input0_generation = 0;
    uint64_t m_cached_input1_generation = 0;

    static constexpr double COMBINER_LOSS_DB = 3.010299956639812;
};
