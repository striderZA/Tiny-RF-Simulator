// Project-file round trip for the budget calculator.
//
// The serializer is app-bound, so these cases drive the real save/newProject/load
// path through RfSimulatorApp rather than the pure JSON helpers in
// test_budget_calculator.cpp. A standalone executable rather than a case in the
// main `tests` binary: MinGW-w64 silently drops TEST_CASEs registered past that
// binary's ceiling (see tests/CMakeLists.txt).
#include "app.h"
#include "budget_calculator.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "test_temp_paths.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using Catch::Approx;

namespace fs = std::filesystem;

namespace {
struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
    }
    ~ImGuiFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

// Absolute temp path: this standalone executable runs with the source tree as
// its CTest working directory, so a relative name would write into it.
fs::path tempPath(const char *stem) {
    static unsigned sequence = 0;
    return fs::temp_directory_path() / (std::string(stem) + test_temp_paths::processTag() + "_" +
                                        std::to_string(++sequence) + ".rfsim");
}
} // namespace

TEST_CASE_METHOD(ImGuiFixture, "Budget round-trips through the project file", "[budget][project]") {
    RfSimulatorApp app;
    auto &budget = app.testBudgetConfig();
    budget.input_power_dBm = -25.0;
    BudgetStage stage;
    stage.type = "amplifier";
    stage.part_number = "AM1143";
    stage.parameters = {{"gain_dB", 30.0}};
    budget.stages.push_back(stage);

    const auto path = tempPath("rfsim_budget_");
    REQUIRE(app.saveProject(path.string()));
    app.newProject();
    REQUIRE(app.testBudgetConfig().stages.empty());
    REQUIRE(app.loadProject(path.string()));
    REQUIRE(app.testBudgetConfig().input_power_dBm == Approx(-25.0));
    REQUIRE(app.testBudgetConfig().stages.size() == 1);
    REQUIRE(app.testBudgetConfig().stages[0].part_number == "AM1143");
    REQUIRE(app.testBudgetConfig().stages[0].type == "amplifier");
    REQUIRE(app.testBudgetConfig().stages[0].parameters["gain_dB"].get<double>() == Approx(30.0));
    std::remove(path.string().c_str());
}

TEST_CASE_METHOD(ImGuiFixture, "Malformed budget stage is isolated on load", "[budget][project]") {
    const auto path = tempPath("rfsim_budget_malformed_");
    std::remove(path.string().c_str());
    {
        std::ofstream out(path);
        REQUIRE(out);
        out << R"json({
            "version": 1,
            "components": [],
            "links": [],
            "probe_pins": [],
            "groups": [],
            "network_analyzer": {},
            "window_state": {},
            "graph_state": {},
            "budget_calculator": {
                "version": 1,
                "input_power_dbm": -12.5,
                "stages": [
                    {"type": "amplifier", "part_number": "AM1143"},
                    {"type": 42, "part_number": "BROKEN"}
                ]
            }
        })json";
    }

    RfSimulatorApp app;
    REQUIRE(app.loadProject(path.string()));
    REQUIRE(app.testBudgetConfig().input_power_dBm == Approx(-12.5));
    REQUIRE(app.testBudgetConfig().stages.size() == 1);
    REQUIRE(app.testBudgetConfig().stages[0].part_number == "AM1143");
    std::remove(path.string().c_str());
}

TEST_CASE_METHOD(ImGuiFixture, "New project clears the budget", "[budget][project]") {
    RfSimulatorApp app;
    app.testBudgetConfig().stages.push_back(BudgetStage{});
    app.newProject();
    REQUIRE(app.testBudgetConfig().stages.empty());
}
