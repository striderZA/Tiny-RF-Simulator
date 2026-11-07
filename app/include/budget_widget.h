#pragma once

#include "budget_calculator.h"
#include "budget_stage_evaluator.h"
#include "component_library.h"
#include "receiver_requirements.h"

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

// Budget cascade editor: toolbar, library picker, and the per-stage metric table.
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

    // Appends a library part to the cascade. Returns false (and changes nothing) for a type
    // the budget has no model for, such as a generator.
    bool addStage(BudgetConfig &budget, const ComponentDefinition &definition);
    void removeStage(BudgetConfig &budget, std::size_t index);
    // Moves one stage by `delta` positions. Returns whether the order changed.
    bool moveStage(BudgetConfig &budget, std::size_t index, int delta);
    void clearStages(BudgetConfig &budget);
    std::size_t stageCount(const BudgetConfig &budget) const;

    // The cursor snaps to the nearest grid point. Before the first draw the value is kept
    // as requested; draw() then clamps it to the first/last point of the grid.
    void setCursor_Hz(double hz);
    double cursor_Hz() const;

    std::function<void()> onChange;

  private:
    BudgetStageEvaluator m_evaluator;
    std::string m_result_key;
    std::vector<BudgetStageEvaluation> m_evaluations;
    BudgetResult m_budget_result;
    std::vector<double> m_frequencies_Hz;
    double m_cursor_Hz = 0.0;
    std::size_t m_cursor_index = 0;
    bool m_cursor_set = false;
    int m_selected_stage = -1;
    std::array<char, 128> m_part_filter{};
};
