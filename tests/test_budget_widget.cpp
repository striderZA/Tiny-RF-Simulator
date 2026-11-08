#include "budget_widget.h"
#include "component_library.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using Catch::Approx;

namespace {

struct BudgetImGuiFixture {
    BudgetImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
        ImGui::GetIO().DisplaySize = ImVec2(1920, 1080);
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char *atlas_pixels = nullptr;
        int atlas_w = 0;
        int atlas_h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&atlas_pixels, &atlas_w, &atlas_h);
    }
    ~BudgetImGuiFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

std::string libraryRoot() { return std::string(PROJECT_SOURCE_DIR) + "/component_data/library"; }

const ComponentDefinition *findPart(const ComponentLibrary &library, const std::string &part) {
    for (const ComponentDefinition *definition : library.all()) {
        if (definition && definition->part_number == part)
            return definition;
    }
    return nullptr;
}

// Snapshot a library definition the way the picker adds it to the budget.
BudgetStage stageFor(const ComponentDefinition &definition) {
    BudgetStage stage;
    stage.type = definition.type;
    stage.part_number = definition.part_number;
    stage.manufacturer = definition.manufacturer;
    stage.description = definition.description;
    stage.parameters = definition.parameters;
    stage.data_files = definition.data_files;
    stage.source_path = definition.source_path;
    return stage;
}

// draw() takes the simulation traces and requirement evaluation the app already has; these
// tests do not exercise the result plots, so they stay empty.
void drawWidget(BudgetWidget &widget, BudgetConfig &config, ComponentLibrary &library,
                const std::vector<double> &frequencies_Hz) {
    const std::vector<double> empty;
    const ReceiverRequirementsState requirements;
    const ReceiverRequirementsEvaluation simulation_result;
    bool open = true;
    ImGui::NewFrame();
    widget.draw("Budget", &open, config, library, frequencies_Hz, empty, empty, empty, empty,
                requirements, simulation_result, -30.0);
    ImGui::Render();
}

} // namespace

TEST_CASE_METHOD(BudgetImGuiFixture, "Budget widget adds, reorders, and removes library stages",
                 "[budget][widget]") {
    ComponentLibrary library;
    library.scan(libraryRoot());
    const auto *am = findPart(library, "AM1143");
    const auto *filter = findPart(library, "MFLP-00033CSP1");
    REQUIRE(am != nullptr);
    REQUIRE(filter != nullptr);

    BudgetConfig config;
    BudgetWidget widget;
    REQUIRE(widget.addStage(config, *am));
    REQUIRE(widget.addStage(config, *filter));
    REQUIRE(widget.stageCount(config) == 2);
    REQUIRE(widget.moveStage(config, 1, -1));
    REQUIRE(config.stages[0].part_number == "MFLP-00033CSP1");
    widget.removeStage(config, 0);
    REQUIRE(widget.stageCount(config) == 1);
    widget.clearStages(config);
    REQUIRE(widget.stageCount(config) == 0);

    ComponentDefinition generator;
    generator.type = "generator";
    REQUIRE_FALSE(widget.addStage(config, generator));
}

TEST_CASE_METHOD(BudgetImGuiFixture, "Budget cursor clamps to the sweep", "[budget][widget]") {
    ComponentLibrary library;
    library.scan(libraryRoot());
    const auto *am = findPart(library, "AM1143");
    REQUIRE(am != nullptr);

    BudgetConfig config;
    BudgetWidget widget;
    REQUIRE(widget.addStage(config, *am));

    const std::vector<double> frequencies_Hz{1.0e9, 2.0e9};
    widget.setCursor_Hz(3.0e9);
    drawWidget(widget, config, library, frequencies_Hz);

    REQUIRE(widget.cursor_Hz() == Approx(2.0e9));
}

TEST_CASE_METHOD(BudgetImGuiFixture, "Budget widget draws the table without a crash",
                 "[budget][widget]") {
    ComponentLibrary library;
    library.scan(libraryRoot());
    const auto *am = findPart(library, "AM1143");
    REQUIRE(am != nullptr);

    BudgetConfig config;
    BudgetWidget widget;
    REQUIRE(widget.addStage(config, *am));

    const std::vector<double> frequencies_Hz{1.0e9, 1.5e9, 2.0e9};
    drawWidget(widget, config, library, frequencies_Hz);

    REQUIRE(widget.stageCount(config) == 1);
    // The cursor defaults to the sweep midpoint; with no requirement band it snaps to 1.5 GHz.
    REQUIRE(widget.cursor_Hz() == Approx(1.5e9));
}

TEST_CASE_METHOD(BudgetImGuiFixture, "Budget widget draws rejected stages and a version notice",
                 "[budget][widget]") {
    ComponentLibrary library;

    // A retained rejected entry is drawn as an extra row and is not part of the cascade.
    BudgetConfig config;
    BudgetRejectedStage rejected;
    rejected.raw = {{"type", 5}, {"part_number", "BROKEN"}};
    rejected.reason = "`type` must be a string";
    config.rejected_stages.push_back(rejected);
    BudgetWidget widget;
    drawWidget(widget, config, library, {1.0e9});
    REQUIRE(widget.stageCount(config) == 0);

    // An unsupported-version section shows its notice and contributes no stages. The test
    // harness cannot read drawn ImGui text, so the row/notice content is not asserted here;
    // the label text is covered indirectly by the JSON retention tests.
    BudgetConfig unsupported;
    unsupported.unsupported_section = {{"version", 2}};
    unsupported.unsupported_reason = "Budget: unsupported version";
    drawWidget(widget, unsupported, library, {1.0e9});
    REQUIRE(widget.stageCount(unsupported) == 0);
    // Adding a stage would be discarded because save writes the raw section verbatim.
    ComponentDefinition part;
    part.type = "amplifier";
    part.part_number = "NEW";
    REQUIRE_FALSE(widget.addStage(unsupported, part));
    REQUIRE(widget.stageCount(unsupported) == 0);
}

TEST_CASE_METHOD(BudgetImGuiFixture, "Budget cumulative rows are prefix cascades",
                 "[budget][widget]") {
    BudgetConfig config;
    config.stages.resize(2); // computeBudget never reads the stage fields, only the count
    const std::vector<double> frequencies_Hz{1.0e9};

    BudgetStageMetrics first;
    first.gain_dB = {20.0};
    first.nf_dB = {3.0};
    first.oip3_dBm = {30.0};
    first.p1db_dBm = {std::numeric_limits<double>::infinity()};
    first.out_of_band = {false};

    BudgetStageMetrics second;
    second.gain_dB = {6.0};
    second.nf_dB = {10.0};
    second.oip3_dBm = {std::numeric_limits<double>::infinity()};
    second.p1db_dBm = {std::numeric_limits<double>::infinity()};
    second.out_of_band = {false};

    BudgetWidget widget;
    const auto rows = widget.rowCascades(config, frequencies_Hz, {first, second});
    REQUIRE(rows.size() == 2);

    // Row 0 is the chain through stage 0; row 1 adds stage 1 instead of repeating the total.
    REQUIRE(rows[0].gain_dB[0] == Approx(20.0));
    REQUIRE(rows[1].gain_dB[0] == Approx(26.0));
    REQUIRE(rows[0].nf_dB[0] == Approx(3.0));
    REQUIRE(rows[1].nf_dB[0] == Approx(3.191607).margin(0.0001));
    REQUIRE(rows[0].gain_dB[0] != Approx(rows[1].gain_dB[0]));
    REQUIRE(rows[0].nf_dB[0] != Approx(rows[1].nf_dB[0]));
}

TEST_CASE_METHOD(BudgetImGuiFixture, "Budget cumulative OIP3 is output-referred",
                 "[budget][widget]") {
    BudgetConfig config;
    config.stages.resize(1);
    const std::vector<double> frequencies_Hz{1.0e9};

    BudgetStageMetrics stage;
    stage.gain_dB = {20.0};
    stage.nf_dB = {3.0};
    stage.oip3_dBm = {30.0};
    stage.p1db_dBm = {std::numeric_limits<double>::infinity()};
    stage.out_of_band = {false};

    BudgetWidget widget;
    auto rows = widget.rowCascades(config, frequencies_Hz, {stage});
    REQUIRE(rows.size() == 1);
    // computeBudget reports input-referred IIP3: 30 dBm output intercept minus the 20 dB gain.
    REQUIRE(rows[0].iip3_dBm[0] == Approx(10.0));
    // The table's `Cum. OIP3` is output-referred again, equal to the stage's own 30 dBm.
    REQUIRE(BudgetWidget::outputReferredOip3_dBm(rows[0], 0) == Approx(30.0));

    // A passive has no OIP3 model (+infinity); the placeholder is preserved.
    BudgetStageMetrics passive = stage;
    passive.oip3_dBm = {std::numeric_limits<double>::infinity()};
    const auto passive_rows = widget.rowCascades(config, frequencies_Hz, {passive});
    REQUIRE(std::isinf(BudgetWidget::outputReferredOip3_dBm(passive_rows[0], 0)));
}

TEST_CASE_METHOD(BudgetImGuiFixture, "Budget stage moves rotate the cascade", "[budget][widget]") {
    BudgetConfig config;
    for (const char *part : {"A", "B", "C", "D"}) {
        BudgetStage stage;
        stage.part_number = part;
        config.stages.push_back(stage);
    }

    BudgetWidget widget;
    REQUIRE(widget.moveStage(config, 0, 2));
    REQUIRE(config.stages[0].part_number == "B");
    REQUIRE(config.stages[1].part_number == "C");
    REQUIRE(config.stages[2].part_number == "A");
    REQUIRE(config.stages[3].part_number == "D");

    // A move that would leave the table is refused; a zero delta is a no-op.
    REQUIRE_FALSE(widget.moveStage(config, 0, 4));
    REQUIRE_FALSE(widget.moveStage(config, 1, 0));

    REQUIRE(widget.moveStage(config, 3, -3));
    REQUIRE(config.stages[0].part_number == "D");
    REQUIRE(config.stages[1].part_number == "B");
    REQUIRE(config.stages[2].part_number == "C");
    REQUIRE(config.stages[3].part_number == "A");
}

TEST_CASE_METHOD(BudgetImGuiFixture, "Budget status reuses the receiver requirement rules",
                 "[budget][widget]") {
    ComponentLibrary library;
    library.scan(std::string(PROJECT_SOURCE_DIR) + "/component_data/library");
    const auto *am = findPart(library, "AM1143");
    REQUIRE(am != nullptr);

    BudgetConfig config;
    config.stages.push_back(stageFor(*am));
    // AM1143's placeholder S-parameter file would dominate the predicted curve above 1 GHz
    // (S21 falls to about 23 dB at 2 GHz). Drop it so the test exercises the datasheet
    // scalars the brief names: 30 dB gain and 1.5 dB noise figure.
    config.stages[0].data_files.clear();
    ReceiverRequirementsState requirements;
    requirements.config = ReceiverRequirementsConfig{};
    requirements.config->band_start_Hz = 1e9;
    requirements.config->band_stop_Hz = 2e9;
    requirements.config->gain = ReceiverGainLimits{25.0, 35.0};
    requirements.config->nf_max_dB = 1.0;

    BudgetWidget widget;
    ImGui::NewFrame();
    widget.draw("Budget", nullptr, config, library, {1e9, 2e9}, {30.0, 30.0}, {1.5, 1.5},
                {-30.0, -30.0}, {35.0, 35.0}, requirements, ReceiverRequirementsEvaluation{},
                -30.0);
    ImGui::Render();
    REQUIRE(widget.budgetStatus() == ReceiverRequirementStatus::Fail); // NF 1.5 > 1.0
    REQUIRE(widget.simulationStatus() == ReceiverRequirementStatus::Incomplete);

    requirements.config->nf_max_dB = 2.0;
    ReceiverRequirementsEvaluation simulation;
    simulation.overall = ReceiverRequirementStatus::Pass;
    simulation.gain.status = ReceiverRequirementStatus::Pass;
    ImGui::NewFrame();
    widget.draw("Budget", nullptr, config, library, {1e9, 2e9}, {30.0, 30.0}, {1.5, 1.5},
                {-30.0, -30.0}, {35.0, 35.0}, requirements, simulation, -30.0);
    ImGui::Render();
    REQUIRE(widget.budgetStatus() == ReceiverRequirementStatus::Pass);
    // The panel renders the passed-in simulation evaluation unchanged.
    REQUIRE(widget.simulationStatus() == ReceiverRequirementStatus::Pass);
}

TEST_CASE_METHOD(BudgetImGuiFixture, "Passive-only budget reports IIP3 as incomplete",
                 "[budget][widget]") {
    ComponentLibrary library;
    library.scan(std::string(PROJECT_SOURCE_DIR) + "/component_data/library");
    const auto *filter = findPart(library, "MFLP-00033CSP1");
    REQUIRE(filter != nullptr);

    BudgetConfig config;
    config.stages.push_back(stageFor(*filter));
    ReceiverRequirementsState requirements;
    requirements.config = ReceiverRequirementsConfig{};
    requirements.config->band_start_Hz = 1e9;
    requirements.config->band_stop_Hz = 2e9;
    // The ideal filter is a 0 dB passband here: a finite gain curve proves the stage really
    // was evaluated, so the IIP3 metric is missing rather than the whole cascade.
    requirements.config->gain = ReceiverGainLimits{-5.0, 5.0};
    requirements.config->iip3_min_dBm = -10.0;
    requirements.config->measurement_conditions.iip3 =
        ReceiverIIP3TestSettings{1.0e6, -20.0, -10.0, 1.0};

    BudgetWidget widget;
    ImGui::NewFrame();
    widget.draw("Budget", nullptr, config, library, {1e9, 2e9}, {}, {}, {}, {}, requirements,
                ReceiverRequirementsEvaluation{}, -30.0);
    ImGui::Render();
    // A passive has no finite OIP3, so the IIP3 metric is missing rather than satisfied.
    REQUIRE(widget.budgetEvaluation().gain.status == ReceiverRequirementStatus::Pass);
    REQUIRE(widget.budgetEvaluation().iip3.status == ReceiverRequirementStatus::Incomplete);
    REQUIRE(widget.budgetStatus() == ReceiverRequirementStatus::Incomplete);
}

TEST_CASE_METHOD(BudgetImGuiFixture, "Empty budget and an out-of-band stage do not crash",
                 "[budget][widget]") {
    ComponentLibrary library;
    library.scan(std::string(PROJECT_SOURCE_DIR) + "/component_data/library");
    const auto *am = findPart(library, "AM1143");
    REQUIRE(am != nullptr);

    ReceiverRequirementsState requirements;
    requirements.config = ReceiverRequirementsConfig{};
    requirements.config->band_start_Hz = 1e9;
    requirements.config->band_stop_Hz = 2e9;
    requirements.config->gain = ReceiverGainLimits{25.0, 35.0};

    BudgetConfig empty;
    BudgetWidget widget;
    ImGui::NewFrame();
    widget.draw("Budget", nullptr, empty, library, {1e9, 2e9}, {30.0, 30.0}, {1.5, 1.5},
                {-30.0, -30.0}, {35.0, 35.0}, requirements, ReceiverRequirementsEvaluation{},
                -30.0);
    ImGui::Render();
    // A budget with no stages has no predicted curve to evaluate: it is incomplete.
    REQUIRE(widget.budgetStatus() == ReceiverRequirementStatus::Incomplete);

    // AM1143's S-parameter file stops at 10 GHz, so every point above it is out of band.
    BudgetConfig out_of_band;
    out_of_band.stages.push_back(stageFor(*am));
    requirements.config->band_start_Hz = 20e9;
    requirements.config->band_stop_Hz = 21e9;
    ImGui::NewFrame();
    widget.draw("Budget", nullptr, out_of_band, library, {20e9, 21e9}, {}, {}, {}, {}, requirements,
                ReceiverRequirementsEvaluation{}, -30.0);
    ImGui::Render();
    REQUIRE(widget.budgetStatus() == ReceiverRequirementStatus::Incomplete);
}
