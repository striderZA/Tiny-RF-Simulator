#pragma once

#include "component_engine_base.h"
#include "node_graph_engine.h"
#include "signal_node.h"
#include "spectrum.h"
#include <string>

// Reverse single-pole double-throw RF switch: two throw inputs (T1/T2) summed
// into one common output (COM). The selected throw reaches COM through the
// configurable insertion loss; the unselected throw leaks in at the isolation
// floor. Each throw is an independent passive two-port, the same model the
// Attenuator/Combiner engines use:
//   noise_W       = G_IL*noise_sel + G_ISO*noise_unsel
//   noise_added_W = k*T*max(0, 1 - G_IL - G_ISO)
class RFSwitch2to1Engine : public ComponentEngineBase {
  public:
    RFSwitch2to1Engine(int id, NodeGraphEngine &graph);

    std::string_view type_name() const override { return "rf_switch_spdt_2to1"; }
    std::string hoverSummary() const override;

    // Declaring the indexed overload hides the inherited no-arg override, so
    // both must be re-declared (same pattern as RFSwitchEngine/CombinerEngine).
    int inputPinId() const override { return inputPinId(0); }
    int inputPinId(int index) const override;
    int outputPinId() const override { return outputPinId(0); }
    int outputPinId(int index) const override;
    int numInputPins() const override { return 2; }

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
    // Two inputs, so ComponentEngineBase::beginUpdate() (single-input) does not
    // apply. Mirrors the CombinerEngine prologue: recompute only when m_dirty is
    // set or either input's pointer/generation changed.
    bool beginUpdate2(const Spectrum *in0, const Spectrum *in1);

    int m_active_throw = 0; // 0 -> T1 in, 1 -> T2 in
    double m_insertion_loss_dB = DEFAULT_INSERTION_LOSS_DB;
    double m_isolation_dB = DEFAULT_ISOLATION_DB;

    const Spectrum *m_cached_input0_ptr = nullptr;
    const Spectrum *m_cached_input1_ptr = nullptr;
    uint64_t m_cached_input0_generation = 0;
    uint64_t m_cached_input1_generation = 0;
};
