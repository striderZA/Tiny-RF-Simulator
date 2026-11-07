#include "budget_widget.h"
#include "component_library.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
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
