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
#include <nlohmann/json.hpp>
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

nlohmann::json readProject(const fs::path &path) {
    std::ifstream in(path);
    nlohmann::json root;
    in >> root;
    return root;
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

TEST_CASE_METHOD(ImGuiFixture, "Malformed budget stage is retained on load and save",
                 "[budget][project]") {
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
    const auto &budget = app.testBudgetConfig();
    REQUIRE(budget.input_power_dBm == Approx(-12.5));
    REQUIRE(budget.stages.size() == 1);
    REQUIRE(budget.stages[0].part_number == "AM1143");
    REQUIRE(budget.rejected_stages.size() == 1);
    REQUIRE(budget.rejected_stages[0].raw["type"] == 42);
    REQUIRE(budget.rejected_stages[0].raw["part_number"] == "BROKEN");
    REQUIRE_FALSE(budget.rejected_stages[0].reason.empty());

    // Saving after the load must not lose the unreadable entry.
    const auto saved_path = tempPath("rfsim_budget_malformed_saved_");
    REQUIRE(app.saveProject(saved_path.string()));
    const auto saved = readProject(saved_path);
    REQUIRE(saved["budget_calculator"]["stages"].size() == 2);
    REQUIRE(saved["budget_calculator"]["stages"][1]["type"] == 42);
    REQUIRE(saved["budget_calculator"]["stages"][1]["part_number"] == "BROKEN");
    std::remove(path.string().c_str());
    std::remove(saved_path.string().c_str());
}

TEST_CASE_METHOD(ImGuiFixture, "Unsupported budget stage type is retained on load and save",
                 "[budget][project]") {
    const auto path = tempPath("rfsim_budget_unsupported_");
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
                    {"type": "generator", "part_number": "GEN1"},
                    {"type": "amplifier", "part_number": "AM1143"},
                    {"type": "ideal_filter", "part_number": "FILT"}
                ]
            }
        })json";
    }

    RfSimulatorApp app;
    REQUIRE(app.loadProject(path.string()));
    // The generator is kept as a rejected entry instead of reaching the two-port probe; the
    // rest survives and the alias normalizes to the registry name.
    const auto &budget = app.testBudgetConfig();
    REQUIRE(budget.stages.size() == 2);
    REQUIRE(budget.stages[0].part_number == "AM1143");
    REQUIRE(budget.stages[1].part_number == "FILT");
    REQUIRE(budget.stages[1].type == "filter");
    REQUIRE(budget.rejected_stages.size() == 1);
    REQUIRE(budget.rejected_stages[0].raw["part_number"] == "GEN1");

    // The rejected entry keeps its original position in the saved array.
    const auto saved_path = tempPath("rfsim_budget_unsupported_saved_");
    REQUIRE(app.saveProject(saved_path.string()));
    const auto saved = readProject(saved_path);
    const auto &stages = saved["budget_calculator"]["stages"];
    REQUIRE(stages.size() == 3);
    REQUIRE(stages[0]["part_number"] == "GEN1");
    REQUIRE(stages[1]["part_number"] == "AM1143");
    REQUIRE(stages[2]["part_number"] == "FILT");
    std::remove(path.string().c_str());
    std::remove(saved_path.string().c_str());
}

TEST_CASE_METHOD(ImGuiFixture, "Unsupported budget version is preserved verbatim",
                 "[budget][project]") {
    const auto path = tempPath("rfsim_budget_version_");
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
                "version": 2,
                "future_field": {"nested": true},
                "input_power_dbm": -12.5,
                "stages": [
                    {"type": "future_part", "part_number": "FUTURE"}
                ]
            }
        })json";
    }

    RfSimulatorApp app;
    REQUIRE(app.loadProject(path.string()));
    const auto &budget = app.testBudgetConfig();
    REQUIRE(budget.stages.empty());
    REQUIRE(budget.rejected_stages.empty());
    REQUIRE(budget.unsupported_section["version"] == 2);
    REQUIRE_FALSE(budget.unsupported_reason.empty());

    // Saving keeps the unknown schema, including fields and stages this build cannot model.
    const auto saved_path = tempPath("rfsim_budget_version_saved_");
    REQUIRE(app.saveProject(saved_path.string()));
    const auto saved = readProject(saved_path);
    const auto &section = saved["budget_calculator"];
    REQUIRE(section["version"] == 2);
    REQUIRE(section["future_field"]["nested"] == true);
    REQUIRE(section["stages"].size() == 1);
    REQUIRE(section["stages"][0]["part_number"] == "FUTURE");

    app.newProject();
    REQUIRE(app.testBudgetConfig().unsupported_section.is_null());
    std::remove(path.string().c_str());
    std::remove(saved_path.string().c_str());
}

TEST_CASE_METHOD(ImGuiFixture, "Empty budget keeps a non-default input power",
                 "[budget][project]") {
    RfSimulatorApp app;
    auto &budget = app.testBudgetConfig();
    budget.stages.clear();
    budget.input_power_dBm = -10.0;

    const auto path = tempPath("rfsim_budget_power_");
    REQUIRE(app.saveProject(path.string()));
    app.newProject();
    REQUIRE(app.testBudgetConfig().input_power_dBm == Approx(-30.0));
    REQUIRE(app.loadProject(path.string()));
    REQUIRE(app.testBudgetConfig().stages.empty());
    REQUIRE(app.testBudgetConfig().input_power_dBm == Approx(-10.0));
    std::remove(path.string().c_str());
}

TEST_CASE_METHOD(ImGuiFixture, "New project clears the budget", "[budget][project]") {
    RfSimulatorApp app;
    app.testBudgetConfig().stages.push_back(BudgetStage{});
    app.newProject();
    REQUIRE(app.testBudgetConfig().stages.empty());
}
