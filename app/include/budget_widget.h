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
    // Moves one stage by `delta` positions, rotating the stages in between. Returns whether
    // the order changed.
    bool moveStage(BudgetConfig &budget, std::size_t index, int delta);
    void clearStages(BudgetConfig &budget);
    std::size_t stageCount(const BudgetConfig &budget) const;

    // One entry per stage row: entry i is `computeBudget` over stages[0..i], so the table's
    // `Cum.` columns show the chain ending at that stage rather than the whole cascade.
    std::vector<BudgetResult>
    rowCascades(const BudgetConfig &budget, const std::vector<double> &frequencies_Hz,
                const std::vector<BudgetStageMetrics> &stage_metrics) const;

    // `Cum. OIP3` cell: `BudgetResult::iip3_dBm` is input-referred, while the stage column is
    // output-referred, so add the cascade gain. Non-finite inputs stay non-finite (placeholder).
    static double outputReferredOip3_dBm(const BudgetResult &cascade, std::size_t point);

    // The cursor snaps to the nearest grid point. Before the first draw the value is kept
    // as requested; draw() then clamps it to the first/last point of the grid.
    void setCursor_Hz(double hz);
    double cursor_Hz() const;

    // Requirement evaluation of the predicted cascade over the sweep. An empty budget has no
    // predicted curve and reports Incomplete. Computed by draw().
    ReceiverRequirementStatus budgetStatus() const;
    // The evaluation the app computed from the live simulation and passed to draw().
    ReceiverRequirementStatus simulationStatus() const;
    const ReceiverRequirementsEvaluation &budgetEvaluation() const;

    std::function<void()> onChange;

  private:
    BudgetStageEvaluator m_evaluator;
    std::string m_result_key;
    std::vector<BudgetStageEvaluation> m_evaluations;
    std::vector<BudgetResult> m_row_cascades;
    BudgetResult m_budget_result;
    ReceiverRequirementsEvaluation m_budget_evaluation;
    ReceiverRequirementsEvaluation m_simulation_result;
    std::vector<double> m_frequencies_Hz;
    std::vector<double> m_simulation_gain_dB;
    std::vector<double> m_simulation_nf_dB;
    std::vector<double> m_simulation_output_power_dBm;
    std::vector<double> m_simulation_iip3_dBm;
    double m_cursor_Hz = 0.0;
    std::size_t m_cursor_index = 0;
    bool m_cursor_set = false;
    int m_selected_stage = -1;
    std::array<char, 128> m_part_filter{};
};
