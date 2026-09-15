#pragma once

#include "component_engine_base.h"
#include "node_graph_engine.h"
#include "signal_node.h"
#include "spectrum.h"
#include <string>

// Single-pole double-throw RF switch: one common input (COM) routed to exactly
// one of two throws (T1/T2). The selected throw is attenuated by the
// configurable insertion loss; the unselected throw still emits the input at
// the isolation floor. Each throw is treated as an independent passive two-port
// with the same model the Attenuator/Combiner engines use:
// noise_total = G*noise_in + k*T*(1 - G).
class RFSwitchEngine : public ComponentEngineBase {
  public:
    RFSwitchEngine(int id, NodeGraphEngine &graph);

    std::string_view type_name() const override { return "rf_switch_spdt"; }
    std::string hoverSummary() const override;

    // Both indexed accessors must stay declared here: ComponentEngineBase's
    // own inputPinId() hides IComponentEngine::inputPinId(int) for a
    // statically-typed RFSwitchEngine, so dropping the pair makes
    // `switch.inputPinId(0)` fail to compile.
    int inputPinId() const override { return inputPinId(0); }
    int inputPinId(int index) const override;
    int outputPinId() const override { return outputPinId(0); }
    int outputPinId(int index) const override;
    int numOutputPins() const override { return 2; }

    void update(double dt) override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json &) override;

    void setActiveThrow(int throw_index);
    int activeThrow() const { return m_active_throw; }

    void setInsertionLoss_dB(double dB);
    double insertionLoss_dB() const { return m_insertion_loss_dB; }

    void setIsolation_dB(double dB);
    double isolation_dB() const { return m_isolation_dB; }

    static constexpr double DEFAULT_INSERTION_LOSS_DB = 0.5;
    static constexpr double DEFAULT_ISOLATION_DB = 40.0;
    static constexpr double MAX_INSERTION_LOSS_DB = 60.0;
    static constexpr double MAX_ISOLATION_DB = 120.0;

  private:
    int m_active_throw = 0; // 0 -> T1, 1 -> T2
    double m_insertion_loss_dB = DEFAULT_INSERTION_LOSS_DB;
    double m_isolation_dB = DEFAULT_ISOLATION_DB;
};
