#pragma once

#include "budget_calculator.h"
#include "component_library.h"
#include "receiver_requirements.h"

#include <functional>
#include <vector>

// Budget cascade editor. This shell only reports its state; the stage table
// and result plots land in follow-up work.
class BudgetWidget {
  public:
    void draw(const char *title, bool *p_open, BudgetConfig &budget, ComponentLibrary &library,
              const std::vector<double> &frequencies_Hz,
              const std::vector<double> &simulation_gain_dB,
              const std::vector<double> &simulation_nf_dB,
              const std::vector<double> &simulation_output_power_dBm,
              const std::vector<double> &simulation_iip3_dBm,
              const ReceiverRequirementsState &requirements,
              const ReceiverRequirementsEvaluation &simulation_result, double stimulus_power_dBm);

    std::function<void()> onChange;
};
