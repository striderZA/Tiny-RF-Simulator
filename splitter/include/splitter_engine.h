#pragma once

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

  private:
    static constexpr double SPLIT_LOSS_DB = 3.010299956639812;
};
