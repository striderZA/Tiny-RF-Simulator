#pragma once

#include "coax_presets.h"
#include "component_engine_base.h"
#include "node_graph_engine.h"
#include "signal_node.h"
#include <string>

class CoaxCableEngine : public ComponentEngineBase {
  public:
    CoaxCableEngine(int id, NodeGraphEngine &graph);

    std::string_view type_name() const override { return "coax"; }

    void update(double dt) override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json &) override;

    std::string hoverSummary() const override;

    void setPresetIndex(int idx);
    void setLengthM(double m);
    void setConnectorsLossDB(double db);

    int presetIndex() const { return m_preset_index; }
    double lengthM() const { return m_length_m; }
    double connectorsLossDB() const { return m_connectors_loss_dB; }
    const CableSpec &preset() const { return kCoaxCablePresets[m_preset_index]; }

  private:
    int m_preset_index = 4; // default to MT 340
    double m_length_m = 1.0;
    double m_connectors_loss_dB = 0.0;
    bool m_warned_above_max = false; // rate-limit flag for over-max_freq warning
};
