#pragma once

#include "component_interface.h"
#include "coax_presets.h"
#include "node_graph_engine.h"
#include "signal_node.h"
#include <string>

class CoaxCableEngine : public IComponentEngine {
  public:
    CoaxCableEngine(int id, NodeGraphEngine& graph);

    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_node_id; }
    int inputPinId() const override;
    int outputPinId() const override;

    void update(double dt) override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json&) override;

    SignalNode& node() override { return m_node; }
    const SignalNode& node() const override { return m_node; }

    std::string hoverSummary() const override;

    void setPresetIndex(int idx);
    void setLengthM(double m);
    void setConnectorsLossDB(double db);

    int presetIndex() const { return m_preset_index; }
    double lengthM() const { return m_length_m; }
    double connectorsLossDB() const { return m_connectors_loss_dB; }
    const CableSpec& preset() const { return kCoaxCablePresets[m_preset_index]; }

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;

    SignalNode m_node;                  // 1 input, 1 output
    int m_preset_index = 4;             // default to MT 340
    double m_length_m = 1.0;
    double m_connectors_loss_dB = 0.0;
    bool m_dirty = true;
    const Spectrum* m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;
    bool m_warned_above_max = false;    // rate-limit flag for over-max_freq warning
};
