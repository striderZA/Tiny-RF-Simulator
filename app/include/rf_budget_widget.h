#pragma once

#include "receiver_requirements.h"
#include "rf_budget.h"
#include "rf_budget_cascade.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class ComponentLibrary;

// Table editor for the requirements-driven RF budget. It edits the model in place and reports
// every accepted change through onChange; a refused change leaves the model unchanged and writes
// one sentence to the widget's error line. Selections and modal drafts are session-only.
class RfBudgetWidget {
  public:
    void draw(const char *title, bool *p_open, RfBudget &budget,
              const ReceiverRequirementsSet &requirements, const ComponentLibrary &library,
              const RfBudgetEvaluation &evaluation);

    std::function<void()> onChange;  // a project edit was accepted
    std::function<void()> onRefresh; // recompute the evaluation

    // The last refusal shown under the toolbar; empty when there is none.
    const std::string &error() const { return m_error; }

  private:
    enum class Modal { None, RenameStage, StageFeeds, StageParams, RenameOutput };

    void drawToolbar(RfBudget &budget);
    void drawStatus(const RfBudget &budget, const RfBudgetEvaluation &evaluation) const;
    void drawStages(RfBudget &budget, const ComponentLibrary &library);
    void drawOutputs(RfBudget &budget);
    void drawOverrides(RfBudget &budget, const ReceiverRequirementsSet &requirements);
    void drawCascade(const RfBudget &budget, const ReceiverRequirementsSet &requirements,
                     const RfBudgetEvaluation &evaluation);
    void drawBandSummary(const RfBudgetCascade &cascade,
                         const RfBudgetEvaluation &evaluation) const;
    void drawPointDetail(const RfBudgetCascade &cascade, const RfBudgetEvaluation &evaluation);
    void drawModals(RfBudget &budget);
    void drawSeriesPlot(const char *label, const char *y_label,
                        const std::vector<double> &frequencies, const RfBudgetSeries &series,
                        std::optional<double> low_limit, std::optional<double> high_limit,
                        float height) const;

    void markChanged();
    void openRenameStage(std::size_t index, const RfBudget &budget);
    void openRenameOutput(std::size_t index, const RfBudget &budget);

    Modal m_modal = Modal::None;
    std::size_t m_modal_stage = 0;
    std::size_t m_modal_output = 0;
    std::array<char, 96> m_name_buffer{};
    std::string m_error;

    bool m_initialized = false;
    std::size_t m_last_requirements_selected = 0;
    std::size_t m_override_mode = 0;
    std::size_t m_override_stage = 0;
    std::size_t m_override_key = 0;
    std::size_t m_cascade_mode = 0;
    std::size_t m_cascade_output = 0;
    int m_cascade_point = 0;
};
