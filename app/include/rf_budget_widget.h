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

    // {x0, y0, x1, y1} of the last draw()'s interactive items. A headless frame has no
    // label-based item lookup without the ImGui Test Engine, so the widget tests press at
    // these rects to drive a real click (the TestFlowWidget::lastButtonRects() pattern).
    // A rect the last draw did not produce stays all-zero; `feed_unfed` is true when that
    // port's feed combo was drawn with an unfed warning preview.
    struct StageRects {
        float block[4] = {};
        float type_combo[4] = {};
        float source_combo[4] = {};
        float part_combo[4] = {};    // library source only
        float params_button[4] = {}; // idealized source only
        float feed_combo[2][4] = {};
        bool feed_unfed[2] = {};
        float rename_button[4] = {};
        float remove_button[4] = {};
    };
    struct ItemRects {
        float add_stage[4] = {};
        float add_output[4] = {};
        float input_block[4] = {};
        float outputs_block[4] = {};
        float chain_scroll_max_x = 0.0f;
        float cascade_mode_combo[4] = {};
        float cascade_output_combo[4] = {};
        std::vector<StageRects> stages;
    };
    const ItemRects &lastItemRects() const { return m_item_rects; }

  private:
    enum class Modal { None, RenameStage, StageParams, RenameOutput };

    void drawToolbar(RfBudget &budget);
    void drawStatus(RfBudget &budget, const RfBudgetEvaluation &evaluation);
    // The horizontal signal-chain strip: budget input block, one block per stage,
    // outputs block. Stages read left to right in table order; the strip scrolls
    // horizontally when the dock is narrower than the chain.
    void drawSignalChain(RfBudget &budget, const ComponentLibrary &library);
    bool drawStageBlock(RfBudget &budget, const ComponentLibrary &library, std::size_t index);
    void drawOutputsBlock(RfBudget &budget);
    void drawOverrides(RfBudget &budget, const ReceiverRequirementsSet &requirements);
    void drawCascade(const RfBudget &budget, const ReceiverRequirementsSet &requirements,
                     const RfBudgetEvaluation &evaluation);
    void drawBandSummary(const RfBudgetCascade &cascade, const RfBudgetEvaluation &evaluation,
                         double band_start_Hz, double band_stop_Hz) const;
    void drawPointDetail(const RfBudgetCascade &cascade, const RfBudgetEvaluation &evaluation);
    void drawModals(RfBudget &budget);
    void drawSeriesPlot(const char *label, const char *y_label,
                        const std::vector<double> &frequencies, const RfBudgetSeries &series,
                        std::optional<double> low_limit, std::optional<double> high_limit,
                        double band_start_Hz, double band_stop_Hz, float height) const;

    void markChanged();
    void openRenameStage(std::size_t index, const RfBudget &budget);
    void openRenameOutput(std::size_t index, const RfBudget &budget);

    Modal m_modal = Modal::None;
    std::size_t m_modal_stage = 0;
    std::size_t m_modal_output = 0;
    std::array<char, 96> m_name_buffer{};
    std::string m_error;
    ItemRects m_item_rects;

    bool m_initialized = false;
    std::size_t m_last_requirements_selected = 0;
    std::size_t m_override_mode = 0;
    std::size_t m_override_stage = 0;
    std::size_t m_override_key = 0;
    std::size_t m_cascade_mode = 0;
    std::size_t m_cascade_output = 0;
    int m_cascade_point = 0;
};
