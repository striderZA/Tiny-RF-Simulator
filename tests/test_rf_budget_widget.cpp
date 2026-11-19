// Headless-frame coverage for the RF Budget view's signal-chain layout
// (View > RF Budget). The stages read as a horizontal signal flow: the budget
// input block on the left, one compact block per stage, the outputs block on
// the right, inside a strip that scrolls horizontally when the dock is
// narrow. The widget records the rects of its interactive items (as
// TestFlowWidget::lastButtonRects() does), because a headless frame has no
// label-based item lookup without the ImGui Test Engine; these tests press at
// those rects with real mouse events.
//
// Coverage map, so a reviewer does not have to reconstruct it: this file
// covers the arrangement (block order, narrow-dock scroll, culled blocks)
// and the button-driven edits (add/remove/rename, unfed warnings, the
// 32-stage cap, the cascade selector one-line/wrap layouts). It does NOT
// click combo options: driving a popup's option rows headlessly means
// computing ImGui-internal popup geometry, which is deliberately out of
// scope here. Combo wiring is covered where it already lives: the model
// helpers (feed/type/override mutations) in tests/test_rf_budget.cpp, the
// agent-facing budget contract in tests/test_agent_budget.cpp, and real
// label-driven clicks (Add Stage, Add Output, Budget this mode,
// Add override) in test_engine/ui_tests.cpp, which runs as the test_ui
// CTest target.
#include "component_library.h"
#include "imgui.h"
#include "implot.h"
#include "receiver_requirements.h"
#include "rf_budget.h"
#include "rf_budget_cascade.h"
#include "rf_budget_widget.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <functional>

namespace {

struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        // A bare context has a (-1,-1) DisplaySize sentinel, and with no
        // renderer backend the font atlas must be built explicitly (NewFrame
        // asserts TexIsBuilt).
        ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char *atlas_pixels = nullptr;
        int atlas_w = 0;
        int atlas_h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&atlas_pixels, &atlas_w, &atlas_h);
    }
    ~ImGuiFixture() {
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

// Three wired stages (amplifier -> splitter -> filter): the amplifier is fed
// from the budget input, the splitter and the filter from the stage before
// them, and one output names the splitter's second port.
RfBudget wiredBudget() {
    RfBudget budget;
    std::string error;
    REQUIRE(addRfBudgetStage(budget, "amplifier", error));
    REQUIRE(addRfBudgetStage(budget, "splitter", error));
    REQUIRE(addRfBudgetStage(budget, "filter", error));
    budget.stages[1].inputs[0] = RfBudgetFeed{std::string("Stage 1"), 0};
    budget.stages[2].inputs[0] = RfBudgetFeed{std::string("Stage 2"), 0};
    REQUIRE(addRfBudgetOutput(budget, 1, 1, error));
    return budget;
}

// One headless frame drawing the RF Budget view in a pinned `size` window; an
// optional `after` runs between the widget's draw and the frame's render, for
// queries that are only valid while the frame is current.
void drawBudgetFrame(RfBudgetWidget &widget, RfBudget &budget, ImVec2 size,
                     const std::function<void()> &after = nullptr) {
    ReceiverRequirementsSet requirements;
    ComponentLibrary library;
    RfBudgetEvaluation evaluation;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    widget.draw("RF Budget", nullptr, budget, requirements, library, evaluation);
    if (after)
        after();
    ImGui::Render();
}

// One headless frame with a caller-built requirements set and evaluation, for
// the cascade section's selector layout.
void drawBudgetFrame(RfBudgetWidget &widget, RfBudget &budget,
                     const ReceiverRequirementsSet &requirements,
                     const RfBudgetEvaluation &evaluation, ImVec2 size) {
    ComponentLibrary library;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    widget.draw("RF Budget", nullptr, budget, requirements, library, evaluation);
    ImGui::Render();
}

ImVec2 rectCenter(const float rect[4]) {
    return ImVec2((rect[0] + rect[2]) * 0.5f, (rect[1] + rect[3]) * 0.5f);
}

bool rectIsEmpty(const float rect[4]) { return rect[2] <= rect[0] || rect[3] <= rect[1]; }

// A real click at `rect`: the pointer is parked off-window for a frame,
// pressed on the next, released on the one after.
void clickRect(RfBudgetWidget &widget, RfBudget &budget, ImVec2 size, const float rect[4]) {
    const ImVec2 centre = rectCenter(rect);
    ImGui::GetIO().AddMousePosEvent(2.0f, 2.0f);
    drawBudgetFrame(widget, budget, size);
    ImGui::GetIO().AddMousePosEvent(centre.x, centre.y);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    drawBudgetFrame(widget, budget, size);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    drawBudgetFrame(widget, budget, size);
}

} // namespace

TEST_CASE_METHOD(ImGuiFixture, "RF Budget cascade selectors share a line only when they fit",
                 "[rf_budget][widget]") {
    RfBudget budget = wiredBudget();

    ReceiverRequirementsSet requirements;
    ReceiverRequirementsConfig config;
    config.band_start_Hz = 30.0e6;
    config.band_stop_Hz = 70.0e6;
    requirements.modes.push_back({"HF receive", config, 1});
    requirements.next_mode_id = 2;

    RfBudgetStageResult stage;
    stage.stage = "Preamp";
    RfBudgetCascade cascade;
    cascade.output = "OUT1";
    cascade.frequencies_Hz = {30.0e6, 50.0e6, 70.0e6};
    cascade.stages.push_back(stage);
    RfBudgetModeResult mode;
    mode.mode_id = 1;
    mode.mode_name = "HF receive";
    mode.status = RfBudgetModeStatus::Budgeted;
    mode.cascades.push_back(cascade);
    RfBudgetEvaluation evaluation;
    evaluation.modes.push_back(mode);

    RfBudgetWidget widget;
    // A wide dock: the output selector rides the mode selector's line.
    const ImVec2 wide(900.0f, 760.0f);
    drawBudgetFrame(widget, budget, requirements, evaluation, wide);
    drawBudgetFrame(widget, budget, requirements, evaluation, wide);
    const RfBudgetWidget::ItemRects &wide_rects = widget.lastItemRects();
    REQUIRE_FALSE(rectIsEmpty(wide_rects.cascade_mode_combo));
    REQUIRE_FALSE(rectIsEmpty(wide_rects.cascade_output_combo));
    REQUIRE(wide_rects.cascade_output_combo[0] > wide_rects.cascade_mode_combo[0]);
    REQUIRE(std::abs(wide_rects.cascade_output_combo[1] - wide_rects.cascade_mode_combo[1]) < 0.5f);

    // A narrow dock (a 420 px window at x = 20): the output selector wraps to
    // its own row and stays inside the window instead of overflowing the line
    // and growing a window scrollbar.
    const ImVec2 narrow(420.0f, 760.0f);
    drawBudgetFrame(widget, budget, requirements, evaluation, narrow);
    drawBudgetFrame(widget, budget, requirements, evaluation, narrow);
    const RfBudgetWidget::ItemRects &narrow_rects = widget.lastItemRects();
    REQUIRE_FALSE(rectIsEmpty(narrow_rects.cascade_output_combo));
    REQUIRE(narrow_rects.cascade_output_combo[1] > narrow_rects.cascade_mode_combo[1]);
    REQUIRE(narrow_rects.cascade_output_combo[2] < 20.0f + narrow.x);
}

TEST_CASE_METHOD(ImGuiFixture, "RF Budget stages read left to right as a signal chain",
                 "[rf_budget][widget]") {
    RfBudget budget = wiredBudget();
    RfBudgetWidget widget;
    const ImVec2 size(1200.0f, 760.0f);
    drawBudgetFrame(widget, budget, size);
    drawBudgetFrame(widget, budget, size);

    const RfBudgetWidget::ItemRects &rects = widget.lastItemRects();
    REQUIRE(rects.stages.size() == 3);

    // The budget input block sits left of every stage, the stages run left to
    // right in table order, and the outputs block closes the chain on the
    // right.
    REQUIRE_FALSE(rectIsEmpty(rects.input_block));
    REQUIRE_FALSE(rectIsEmpty(rects.outputs_block));
    REQUIRE(rects.input_block[0] < rects.stages[0].block[0]);
    REQUIRE(rects.stages[0].block[0] < rects.stages[1].block[0]);
    REQUIRE(rects.stages[1].block[0] < rects.stages[2].block[0]);
    REQUIRE(rects.stages[2].block[0] < rects.outputs_block[0]);

    // The strip is one row: every block occupies the same vertical band.
    for (std::size_t index = 0; index < rects.stages.size(); ++index) {
        REQUIRE_FALSE(rectIsEmpty(rects.stages[index].block));
        REQUIRE(rects.stages[index].block[1] < rects.stages[index].block[3]);
    }

    // The wired ports are not flagged as unfed; the feed combos sit inside
    // their stage blocks.
    REQUIRE_FALSE(rects.stages[0].feed_unfed[0]);
    REQUIRE_FALSE(rectIsEmpty(rects.stages[0].feed_combo[0]));
    REQUIRE_FALSE(rectIsEmpty(rects.stages[1].feed_combo[0]));
}

TEST_CASE_METHOD(ImGuiFixture, "RF Budget chain scrolls horizontally when the dock is narrow",
                 "[rf_budget][widget]") {
    RfBudget budget = wiredBudget();
    RfBudgetWidget widget;
    const ImVec2 size(420.0f, 760.0f);
    drawBudgetFrame(widget, budget, size);
    drawBudgetFrame(widget, budget, size);

    // The blocks keep their compact width; the strip carries what does not fit
    // in a horizontal scrollbar.
    const RfBudgetWidget::ItemRects &rects = widget.lastItemRects();
    REQUIRE(rects.stages.size() == 3);
    REQUIRE(rects.chain_scroll_max_x > 0.0f);
    REQUIRE(rects.input_block[0] < rects.stages[0].block[0]);
    REQUIRE(rects.stages[2].block[0] < rects.outputs_block[0]);
}

TEST_CASE_METHOD(ImGuiFixture, "RF Budget empty view shows the hint, not blocks",
                 "[rf_budget][widget]") {
    RfBudget budget;
    RfBudgetWidget widget;
    const ImVec2 size(900.0f, 760.0f);
    drawBudgetFrame(widget, budget, size);
    drawBudgetFrame(widget, budget, size);

    const RfBudgetWidget::ItemRects &rects = widget.lastItemRects();
    REQUIRE(rects.stages.empty());
    REQUIRE(rectIsEmpty(rects.input_block));
    REQUIRE(rectIsEmpty(rects.outputs_block));
}

TEST_CASE_METHOD(ImGuiFixture, "RF Budget toolbar and block buttons edit the model",
                 "[rf_budget][widget]") {
    RfBudget budget;
    RfBudgetWidget widget;
    const ImVec2 size(900.0f, 760.0f);
    drawBudgetFrame(widget, budget, size);
    drawBudgetFrame(widget, budget, size);

    // Add Output is disabled with no stages: pressing its rect changes nothing.
    clickRect(widget, budget, size, widget.lastItemRects().add_output);
    REQUIRE(budget.outputs.empty());

    // Add Stage appends a fed amplifier block; its feed combo is wired to the
    // budget input, so the port is not flagged as unfed.
    clickRect(widget, budget, size, widget.lastItemRects().add_stage);
    REQUIRE(budget.stages.size() == 1);
    REQUIRE(budget.stages[0].name == "Stage 1");
    REQUIRE(budget.stages[0].type == "amplifier");
    REQUIRE(budget.stages[0].inputs[0] == RfBudgetFeed{std::nullopt, 0});

    // Add Output names the first stage's port 0.
    clickRect(widget, budget, size, widget.lastItemRects().add_output);
    REQUIRE(budget.outputs.size() == 1);
    REQUIRE(budget.outputs[0].stage == "Stage 1");
    REQUIRE(budget.outputs[0].port == 0);

    // The idealized source offers the Params editor instead of a part picker.
    const RfBudgetWidget::ItemRects &rects = widget.lastItemRects();
    REQUIRE_FALSE(rectIsEmpty(rects.stages[0].params_button));
    REQUIRE(rectIsEmpty(rects.stages[0].part_combo));
    REQUIRE_FALSE(rects.stages[0].feed_unfed[0]);

    // Rename opens the rename-stage modal.
    bool rename_open = false;
    clickRect(widget, budget, size, rects.stages[0].rename_button);
    // A string IsPopupOpen() hashes against the current window, which is no
    // longer the view once its draw returns; drawModals is the only thing that
    // opens a popup here, so any-open pins the modal.
    drawBudgetFrame(widget, budget, size, [&] {
        rename_open =
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    });
    REQUIRE(rename_open);
}

TEST_CASE_METHOD(ImGuiFixture, "RF Budget Remove drops the stage's feeds and outputs",
                 "[rf_budget][widget]") {
    RfBudget budget = wiredBudget();
    RfBudgetWidget widget;
    const ImVec2 size(1200.0f, 760.0f);
    drawBudgetFrame(widget, budget, size);
    drawBudgetFrame(widget, budget, size);

    // Removing the middle stage (the splitter) drops its output, and the
    // filter's feed from it is reset to unfed.
    clickRect(widget, budget, size, widget.lastItemRects().stages[1].remove_button);
    REQUIRE(budget.stages.size() == 2);
    REQUIRE(budget.stages[0].name == "Stage 1");
    REQUIRE(budget.stages[1].name == "Stage 3");
    REQUIRE(budget.stages[1].inputs[0] == std::nullopt);
    REQUIRE(budget.outputs.empty());

    // The chain redraws with the remaining stages left to right, and the
    // orphaned feed shows the unfed warning.
    drawBudgetFrame(widget, budget, size);
    drawBudgetFrame(widget, budget, size);
    const RfBudgetWidget::ItemRects &rects = widget.lastItemRects();
    REQUIRE(rects.stages.size() == 2);
    REQUIRE(rects.input_block[0] < rects.stages[0].block[0]);
    REQUIRE(rects.stages[0].block[0] < rects.stages[1].block[0]);
    REQUIRE(rects.stages[1].block[0] < rects.outputs_block[0]);
    REQUIRE(rects.stages[1].feed_unfed[0]);
}

TEST_CASE_METHOD(ImGuiFixture, "RF Budget unfed input shows a warning preview",
                 "[rf_budget][widget]") {
    RfBudget budget;
    std::string error;
    REQUIRE(addRfBudgetStage(budget, "amplifier", error));
    REQUIRE(addRfBudgetStage(budget, "rf_switch_spdt_2to1", error));
    RfBudgetWidget widget;
    const ImVec2 size(900.0f, 760.0f);
    drawBudgetFrame(widget, budget, size);
    drawBudgetFrame(widget, budget, size);

    // The first stage is auto-fed from the budget input; the 2:1 switch's two
    // ports are unfed, and the second one is flagged on the recorded slot.
    const RfBudgetWidget::ItemRects &rects = widget.lastItemRects();
    REQUIRE_FALSE(rects.stages[0].feed_unfed[0]);
    REQUIRE(rects.stages[1].feed_unfed[0]);
    REQUIRE(rects.stages[1].feed_unfed[1]);
    REQUIRE_FALSE(rectIsEmpty(rects.stages[1].feed_combo[1]));
}

TEST_CASE_METHOD(ImGuiFixture, "RF Budget chain holds all 32 stages in order",
                 "[rf_budget][widget]") {
    RfBudget budget;
    std::string error;
    while (budget.stages.size() < kMaxRfBudgetStages)
        REQUIRE(addRfBudgetStage(budget, "attenuator", error));
    RfBudgetWidget widget;
    const ImVec2 size(1200.0f, 760.0f);
    drawBudgetFrame(widget, budget, size);
    drawBudgetFrame(widget, budget, size);

    // Every stage gets its own block, in table order, left to right, ending at
    // the outputs block. Blocks scrolled out of the visible strip are culled
    // but still record their layout slot.
    const RfBudgetWidget::ItemRects &rects = widget.lastItemRects();
    REQUIRE(rects.stages.size() == kMaxRfBudgetStages);
    REQUIRE(rects.input_block[0] < rects.stages[0].block[0]);
    for (std::size_t index = 1; index < rects.stages.size(); ++index)
        REQUIRE(rects.stages[index - 1].block[0] < rects.stages[index].block[0]);
    REQUIRE(rects.stages.back().block[0] < rects.outputs_block[0]);
    REQUIRE(rects.chain_scroll_max_x > 0.0f);

    // The cap refuses a 33rd stage with one sentence on the error line.
    clickRect(widget, budget, size, rects.add_stage);
    REQUIRE(budget.stages.size() == kMaxRfBudgetStages);
    REQUIRE_FALSE(widget.error().empty());
}
