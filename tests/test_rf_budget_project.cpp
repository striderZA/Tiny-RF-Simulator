#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "rf_budget.h"
#include "test_temp_paths.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using nlohmann::json;

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

std::string tempPath(const std::string &suffix) {
    return (std::filesystem::temp_directory_path() /
            ("test_rf_budget_project_" + test_temp_paths::processTag() + suffix + ".rfsim"))
        .string();
}

json loadJson(const std::string &path) {
    std::ifstream input(path);
    json value;
    input >> value;
    return value;
}

void saveJson(const std::string &path, const json &value) {
    std::ofstream output(path);
    output << value.dump(2);
}

json validBudget() {
    return {
        {"format", 1},
        {"point_count", 201},
        {"stages",
         json::array({{{"name", "LNA"},
                       {"type", "amplifier"},
                       {"source", "idealized"},
                       {"params", {{"gain_dB", 20.0}}},
                       {"inputs", json::array({json{{"from", "input"}}})}},
                      {{"name", "ATT"},
                       {"type", "attenuator"},
                       {"source", "idealized"},
                       {"params", {{"atten_dB", 6.0}}},
                       {"inputs",
                        json::array({json{{"from", "stage"}, {"stage", "LNA"}, {"port", 0}}})}}})},
        {"outputs", json::array({{{"name", "RX"}, {"stage", "ATT"}, {"port", 0}}})},
        {"modes", json::array({{{"mode_id", 1}, {"overrides", {{"ATT", {{"atten_dB", 10.0}}}}}}})}};
}

json savedProjectWithBudget(const std::string &path, const json &budget) {
    {
        RfSimulatorApp app;
        app.saveProject(path);
    }
    json project = loadJson(path);
    project["rf_budget"] = budget;
    saveJson(path, project);
    return project;
}

RfBudget modelBudget() {
    RfBudget budget;
    RfBudgetStage lna;
    lna.name = "LNA";
    lna.type = "amplifier";
    lna.source = RfBudgetStageSource::Idealized;
    lna.params = {{"gain_dB", 20.0}, {"nf_dB", 2.0}};
    lna.inputs = {RfBudgetFeed{std::nullopt, 0}};
    RfBudgetStage att;
    att.name = "ATT";
    att.type = "attenuator";
    att.source = RfBudgetStageSource::Idealized;
    att.params = {{"atten_dB", 6.0}};
    att.inputs = {RfBudgetFeed{"LNA", 0}};
    budget.point_count = 41;
    budget.stages = {lna, att};
    budget.outputs = {{"RX", "ATT", 0}};
    budget.modes = {{1, {{"ATT", {{"atten_dB", 12.0}}}}}};
    return budget;
}
} // namespace

TEST_CASE_METHOD(ImGuiFixture, "RF budget round-trips through a project file",
                 "[rf_budget][project]") {
    const auto path = tempPath("_round_trip");
    const RfBudget original = modelBudget();
    {
        RfSimulatorApp app;
        app.testRfBudget() = original;
        app.saveProject(path);
    }
    const json saved = loadJson(path);
    REQUIRE(saved.contains("rf_budget"));
    CHECK(saved["rf_budget"]["format"] == kRfBudgetFormat);
    CHECK(saved["rf_budget"]["point_count"] == 41);
    {
        RfSimulatorApp app;
        app.loadProject(path);
        CHECK(app.m_current_project_path == path);
        CHECK(app.testRfBudget() == original);
    }
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "Malformed RF budget keeps the rest of the project",
                 "[rf_budget][project]") {
    const auto path = tempPath("_malformed");
    auto project = savedProjectWithBudget(path, {{"format", 1}, {"stages", "x"}});
    project["receiver_requirements"] = {{"band_start_hz", 1.0e9},
                                        {"band_stop_hz", 2.0e9},
                                        {"gain_min_db", 10.0},
                                        {"gain_max_db", 20.0},
                                        {"nf_max_db", 5.0}};
    saveJson(path, project);

    RfSimulatorApp app;
    app.loadProject(path);
    CHECK(app.m_current_project_path == path);
    CHECK(app.componentCount() == 2);
    CHECK(app.testRfBudget().invalid_reason == "RF budget stages must be a list.");
    CHECK(app.testRfBudget().stages.empty());
    const auto requirements = app.testReceiverRequirementsState();
    REQUIRE(requirements.config.has_value());
    CHECK(requirements.invalid_reason.empty());

    app.saveProject(path);
    const json saved = loadJson(path);
    REQUIRE(saved.contains("rf_budget"));
    REQUIRE(saved["rf_budget"].is_object());
    CHECK(saved["rf_budget"]["invalid_configuration"] == true);
    CHECK(saved["rf_budget"]["diagnostic"] == "RF budget stages must be a list.");
    CHECK(saved.contains("receiver_requirements"));
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "Overflowing RF budget number is invalid, not a parse failure",
                 "[rf_budget][project]") {
    const auto path = tempPath("_overflow");
    savedProjectWithBudget(path, validBudget());
    std::string contents;
    {
        std::ifstream input(path);
        contents.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    const auto field = contents.find("\"point_count\": ");
    REQUIRE(field != std::string::npos);
    const auto value_start = contents.find(':', field) + 1;
    const auto value_end = contents.find(',', value_start);
    REQUIRE(value_end != std::string::npos);
    contents.replace(value_start, value_end - value_start, " 1e400");
    {
        std::ofstream output(path, std::ios::trunc);
        output << contents;
    }

    RfSimulatorApp app;
    app.loadProject(path);
    CHECK(app.m_current_project_path == path);
    CHECK(app.componentCount() == 2);
    CHECK(app.testRfBudget().invalid_reason == "Point count must be an integer.");
    app.saveProject(path);
    const json saved = loadJson(path);
    REQUIRE(saved.contains("rf_budget"));
    CHECK(saved["rf_budget"]["invalid_configuration"] == true);
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "New project clears the RF budget", "[rf_budget][project]") {
    const auto path = tempPath("_new");
    savedProjectWithBudget(path, validBudget());
    RfSimulatorApp app;
    app.loadProject(path);
    CHECK_FALSE(app.testRfBudget().empty());
    app.newProject();
    CHECK(app.testRfBudget().empty());
    app.saveProject(path);
    CHECK_FALSE(loadJson(path).contains("rf_budget"));
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "A project without a budget saves no rf_budget key",
                 "[rf_budget][project]") {
    const auto path = tempPath("_absent");
    RfSimulatorApp app;
    CHECK(app.testRfBudget().empty());
    app.saveProject(path);
    const json saved = loadJson(path);
    CHECK_FALSE(saved.contains("rf_budget"));
    std::filesystem::remove(path);
}
