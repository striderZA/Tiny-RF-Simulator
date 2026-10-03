#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "receiver_requirements.h"
#include "test_temp_paths.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
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
            ("test_receiver_requirements_project_" + test_temp_paths::processTag() + suffix +
             ".rfsim"))
        .string();
}

const json validRequirements = {{"band_start_hz", 1.0e9},
                                {"band_stop_hz", 2.0e9},
                                {"gain_min_db", 10.0},
                                {"gain_max_db", 20.0},
                                {"nf_max_db", 5.0}};
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

json savedProjectWithRequirements(const std::string &path, const json &requirements) {
    {
        RfSimulatorApp app;
        app.saveProject(path);
    }
    json project = loadJson(path);
    project["receiver_requirements"] = requirements;
    saveJson(path, project);
    return project;
}
} // namespace

TEST_CASE_METHOD(ImGuiFixture, "Receiver requirements persist valid project settings",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_valid");
    {
        RfSimulatorApp app;
        app.saveProject(path);
    }
    auto project = loadJson(path);
    project["receiver_requirements"] = validRequirements;
    saveJson(path, project);
    {
        RfSimulatorApp app;
        app.loadProject(path);
        REQUIRE(app.testReceiverRequirementsState().config.has_value());
        REQUIRE(app.testReceiverRequirementsState().config->gain.has_value());
        CHECK(app.testReceiverRequirementsState().config->gain->minimum_dB == 10.0);
        CHECK(app.testReceiverRequirementsState().config->gain->maximum_dB == 20.0);
        CHECK(app.testReceiverRequirementsState().config->nf_max_dB == 5.0);
        CHECK(app.componentCount() == 2);
        app.saveProject(path);
    }
    const json saved = loadJson(path);
    REQUIRE(saved.contains("receiver_requirements"));
    CHECK(saved["receiver_requirements"] == validRequirements);
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "Receiver requirements persist optional metrics and conditions",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_optional");
    RfSimulatorApp app;
    auto &state = app.testReceiverRequirementsState();
    ReceiverRequirementsConfig config;
    config.band_start_Hz = 1.0e9;
    config.band_stop_Hz = 2.0e9;
    config.gain = ReceiverGainLimits{10.0, 20.0};
    config.nf_max_dB = 5.0;
    config.output_power = ReceiverOutputPowerLimits{-30.0, 0.0};
    config.iip3_min_dBm = 10.0;
    config.measurement_conditions.output_reference_tone_frequency_Hz = 1.5e9;
    config.measurement_conditions.iip3 = ReceiverIIP3TestSettings{1.0e6, -30.0, -20.0, 5.0};
    state.config = config;
    app.saveProject(path);

    const auto saved = loadJson(path);
    const auto &requirements = saved["receiver_requirements"];
    CHECK(requirements["output_power_min_dbm"] == -30.0);
    CHECK(requirements["output_power_max_dbm"] == 0.0);
    CHECK(requirements["iip3_min_dbm"] == 10.0);
    CHECK(requirements["measurement_conditions"]["output_reference_tone_frequency_hz"] == 1.5e9);
    CHECK(requirements["measurement_conditions"]["iip3"]["tone_spacing_hz"] == 1.0e6);
    CHECK(requirements["measurement_conditions"]["iip3"]["input_start_dbm"] == -30.0);
    CHECK(requirements["measurement_conditions"]["iip3"]["input_stop_dbm"] == -20.0);
    CHECK(requirements["measurement_conditions"]["iip3"]["input_step_db"] == 5.0);
    app.loadProject(path);
    REQUIRE(state.config.has_value());
    REQUIRE(state.config->output_power.has_value());
    CHECK(state.config->output_power->minimum_dBm == -30.0);
    CHECK(state.config->output_power->maximum_dBm == 0.0);
    CHECK(state.config->iip3_min_dBm == config.iip3_min_dBm);
    REQUIRE(state.config->measurement_conditions.iip3.has_value());
    CHECK(state.config->measurement_conditions.iip3->tone_spacing_Hz == 1.0e6);
    CHECK(state.config->measurement_conditions.output_reference_tone_frequency_Hz == 1.5e9);
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "Receiver requirements persist gain-only and NF-only configs",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_partial");
    RfSimulatorApp app;
    auto &state = app.testReceiverRequirementsState();
    ReceiverRequirementsConfig config;
    config.band_start_Hz = 1.0e9;
    config.band_stop_Hz = 2.0e9;
    config.gain = ReceiverGainLimits{10.0, 20.0};
    state.config = config;
    app.saveProject(path);
    app.loadProject(path);
    REQUIRE(state.config.has_value());
    REQUIRE(state.config->gain.has_value());
    CHECK(state.config->gain->minimum_dB == 10.0);
    CHECK(state.config->gain->maximum_dB == 20.0);
    CHECK_FALSE(state.config->nf_max_dB.has_value());
    CHECK_FALSE(loadJson(path)["receiver_requirements"].contains("nf_max_db"));

    config.gain.reset();
    config.nf_max_dB = 5.0;
    state.config = config;
    app.saveProject(path);
    app.loadProject(path);
    REQUIRE(state.config.has_value());
    CHECK_FALSE(state.config->gain.has_value());
    CHECK(state.config->nf_max_dB == 5.0);
    CHECK_FALSE(loadJson(path)["receiver_requirements"].contains("gain_min_db"));
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "Receiver requirements absent from a legacy project remain absent",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_legacy");
    {
        RfSimulatorApp app;
        app.saveProject(path);
    }
    auto project = loadJson(path);
    project.erase("receiver_requirements");
    saveJson(path, project);
    {
        RfSimulatorApp app;
        app.loadProject(path);
        CHECK(app.componentCount() == 2);
        app.saveProject(path);
    }
    const json saved = loadJson(path);
    CHECK_FALSE(saved.contains("receiver_requirements"));
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture,
                 "Malformed receiver requirements preserve project contents and invalid marker",
                 "[receiver_requirements][project]") {
    SECTION("wrong-shaped object") {
        const auto path = tempPath("_shape");
        savedProjectWithRequirements(path, json(17));
        RfSimulatorApp app;
        app.loadProject(path);
        CHECK(app.componentCount() == 2);
        app.saveProject(path);
        const json saved = loadJson(path);
        REQUIRE(saved.contains("receiver_requirements"));
        REQUIRE(saved["receiver_requirements"].is_object());
        CHECK(saved["receiver_requirements"]["invalid_configuration"] == true);
        CHECK(saved["receiver_requirements"]["diagnostic"].is_string());
        CHECK_FALSE(saved["receiver_requirements"]["diagnostic"].get<std::string>().empty());
        std::filesystem::remove(path);
    }
    SECTION("missing field") {
        auto malformed = validRequirements;
        malformed.erase("band_stop_hz");
        const auto path = tempPath("_missing");
        savedProjectWithRequirements(path, malformed);
        RfSimulatorApp app;
        app.loadProject(path);
        CHECK(app.componentCount() == 2);
        app.saveProject(path);
        const json saved = loadJson(path);
        REQUIRE(saved.contains("receiver_requirements"));
        REQUIRE(saved["receiver_requirements"].is_object());
        CHECK(saved["receiver_requirements"]["invalid_configuration"] == true);
        CHECK(saved["receiver_requirements"]["diagnostic"].is_string());
        std::filesystem::remove(path);
    }
    SECTION("wrong-typed field") {
        auto malformed = validRequirements;
        malformed["gain_min_db"] = "10";
        const auto path = tempPath("_typed");
        savedProjectWithRequirements(path, malformed);
        RfSimulatorApp app;
        app.loadProject(path);
        CHECK(app.componentCount() == 2);
        app.saveProject(path);
        const json saved = loadJson(path);
        REQUIRE(saved.contains("receiver_requirements"));
        REQUIRE(saved["receiver_requirements"].is_object());
        CHECK(saved["receiver_requirements"]["invalid_configuration"] == true);
        CHECK(saved["receiver_requirements"]["diagnostic"].is_string());
        std::filesystem::remove(path);
    }
    SECTION("reversed limits") {
        auto malformed = validRequirements;
        malformed["band_start_hz"] = 2.0e9;
        malformed["band_stop_hz"] = 1.0e9;
        const auto path = tempPath("_reversed");
        savedProjectWithRequirements(path, malformed);
        RfSimulatorApp app;
        app.loadProject(path);
        CHECK(app.componentCount() == 2);
        app.saveProject(path);
        const json saved = loadJson(path);
        REQUIRE(saved.contains("receiver_requirements"));
        REQUIRE(saved["receiver_requirements"].is_object());
        CHECK(saved["receiver_requirements"]["invalid_configuration"] == true);
        CHECK(saved["receiver_requirements"]["diagnostic"].is_string());
        std::filesystem::remove(path);
    }
}

TEST_CASE_METHOD(ImGuiFixture, "Invalid receiver requirements survive save and reload",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_invalid_reload");
    savedProjectWithRequirements(path, json{{"gain_min_db", "bad"}});
    {
        RfSimulatorApp app;
        app.loadProject(path);
        app.saveProject(path);
    }
    {
        RfSimulatorApp app;
        app.loadProject(path);
        CHECK(app.componentCount() == 2);
        app.saveProject(path);
    }
    const json saved = loadJson(path);
    REQUIRE(saved.contains("receiver_requirements"));
    REQUIRE(saved["receiver_requirements"].is_object());
    CHECK(saved["receiver_requirements"]["invalid_configuration"] == true);
    CHECK(saved["receiver_requirements"]["diagnostic"].is_string());
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "New project clears receiver requirements",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_new");
    {
        RfSimulatorApp app;
        app.saveProject(path);
    }
    auto project = loadJson(path);
    project["receiver_requirements"] =
        json{{"band_start_hz", 1.0e9},
             {"band_stop_hz", 2.0e9},
             {"gain_min_db", 10.0},
             {"gain_max_db", 20.0},
             {"nf_max_db", 5.0},
             {"measurement_conditions",
              json{{"output_reference_tone_frequency_hz", 1.5e9},
                   {"iip3", json{{"tone_spacing_hz", 1.0e6},
                                 {"input_start_dbm", -30.0},
                                 {"input_stop_dbm", -20.0},
                                 {"input_step_db", 5.0}}}}}};
    saveJson(path, project);
    {
        RfSimulatorApp app;
        app.loadProject(path);
        CHECK(app.componentCount() == 2);
        REQUIRE(app.testReceiverRequirementsState().config.has_value());
        CHECK(app.testReceiverRequirementsState()
                  .config->measurement_conditions.output_reference_tone_frequency_Hz.has_value());
        REQUIRE(app.testReceiverRequirementsState().config->measurement_conditions.iip3.has_value());
        app.newProject();
        CHECK_FALSE(app.testReceiverRequirementsState().config.has_value());
        CHECK(app.testReceiverRequirementsState().invalid_reason.empty());
        app.saveProject(path);
    }
    const json saved = loadJson(path);
    CHECK_FALSE(saved.contains("receiver_requirements"));
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "New project clears invalid receiver requirements",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_new_invalid");
    savedProjectWithRequirements(path, json{{"band_start_hz", "bad"}});
    RfSimulatorApp app;
    app.loadProject(path);
    CHECK_FALSE(app.testReceiverRequirementsState().config.has_value());
    CHECK_FALSE(app.testReceiverRequirementsState().invalid_reason.empty());
    app.newProject();
    CHECK_FALSE(app.testReceiverRequirementsState().config.has_value());
    CHECK(app.testReceiverRequirementsState().invalid_reason.empty());
    app.saveProject(path);
    CHECK_FALSE(loadJson(path).contains("receiver_requirements"));
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "Malformed persisted receiver requirements are invalid",
                 "[receiver_requirements][project]") {
    const std::vector<std::pair<std::string, json>> malformed_cases = {
        {"wrong_band_start", json{{"band_start_hz", "1e9"},
                                  {"band_stop_hz", 2.0e9},
                                  {"gain_min_db", 10.0},
                                  {"gain_max_db", 20.0}}},
        {"half_gain", json{{"band_start_hz", 1.0e9},
                           {"band_stop_hz", 2.0e9},
                           {"gain_min_db", 10.0}}},
        {"half_output_power", json{{"band_start_hz", 1.0e9},
                                   {"band_stop_hz", 2.0e9},
                                   {"output_power_min_dbm", -30.0}}},
        {"wrong_nf", json{{"band_start_hz", 1.0e9},
                          {"band_stop_hz", 2.0e9},
                          {"nf_max_db", false}}},
        {"wrong_iip3_limit", json{{"band_start_hz", 1.0e9},
                                  {"band_stop_hz", 2.0e9},
                                  {"iip3_min_dbm", "10"}}},
        {"reversed_gain", json{{"band_start_hz", 1.0e9},
                               {"band_stop_hz", 2.0e9},
                               {"gain_min_db", 20.0},
                               {"gain_max_db", 10.0}}},
        {"reversed_output", json{{"band_start_hz", 1.0e9},
                                 {"band_stop_hz", 2.0e9},
                                 {"output_power_min_dbm", 0.0},
                                 {"output_power_max_dbm", -30.0}}},
        {"null_limit", json{{"band_start_hz", nullptr},
                            {"band_stop_hz", 2.0e9},
                            {"gain_min_db", 10.0},
                            {"gain_max_db", 20.0}}},
        {"conditions_array", json{{"band_start_hz", 1.0e9},
                                  {"band_stop_hz", 2.0e9},
                                  {"measurement_conditions", json::array()}}},
        {"wrong_tone_selector", json{{"band_start_hz", 1.0e9},
                                     {"band_stop_hz", 2.0e9},
                                     {"measurement_conditions",
                                      json{{"output_reference_tone_frequency_hz", "1.5e9"}}}}},
        {"null_tone_selector", json{{"band_start_hz", 1.0e9},
                                    {"band_stop_hz", 2.0e9},
                                    {"measurement_conditions",
                                     json{{"output_reference_tone_frequency_hz", nullptr}}}}},
        {"iip3_not_object", json{{"band_start_hz", 1.0e9},
                                 {"band_stop_hz", 2.0e9},
                                 {"measurement_conditions", json{{"iip3", 4}}}}},
        {"iip3_missing_nested_field",
         json{{"band_start_hz", 1.0e9},
              {"band_stop_hz", 2.0e9},
              {"measurement_conditions",
               json{{"iip3", json{{"tone_spacing_hz", 1.0e6},
                                  {"input_start_dbm", -30.0},
                                  {"input_stop_dbm", -20.0}}}}}}},
        {"malformed_iip3_while_disabled",
         json{{"band_start_hz", 1.0e9},
              {"band_stop_hz", 2.0e9},
              {"measurement_conditions",
               json{{"iip3", json{{"tone_spacing_hz", 1.0e6},
                                  {"input_start_dbm", -30.0},
                                  {"input_stop_dbm", -20.0},
                                  {"input_step_db", "5"}}}}}}},
        {"enabled_iip3_without_settings",
         json{{"band_start_hz", 1.0e9}, {"band_stop_hz", 2.0e9}, {"iip3_min_dbm", 10.0}}},
        {"enabled_iip3_with_invalid_settings",
         json{{"band_start_hz", 1.0e9},
              {"band_stop_hz", 2.0e9},
              {"iip3_min_dbm", 10.0},
              {"measurement_conditions",
               json{{"iip3", json{{"tone_spacing_hz", 0.0},
                                  {"input_start_dbm", -30.0},
                                  {"input_stop_dbm", -20.0},
                                  {"input_step_db", 5.0}}}}}}},
        {"iip3_sweep_over_101_levels",
         json{{"band_start_hz", 1.0e9},
              {"band_stop_hz", 2.0e9},
              {"iip3_min_dbm", 10.0},
              {"measurement_conditions",
               json{{"iip3", json{{"tone_spacing_hz", 1.0e6},
                                  {"input_start_dbm", -30.0},
                                  {"input_stop_dbm", -19.9},
                                  {"input_step_db", 0.1}}}}}}}};

    std::size_t index = 0;
    for (const auto &[label, malformed] : malformed_cases) {
        const auto path = tempPath("_malformed_" + label + std::to_string(index++));
        savedProjectWithRequirements(path, malformed);
        RfSimulatorApp app;
        app.loadProject(path);
        CHECK(app.m_current_project_path == path);
        CHECK(app.componentCount() == 2);
        CHECK_FALSE(app.testReceiverRequirementsState().config.has_value());
        CHECK_FALSE(app.testReceiverRequirementsState().invalid_reason.empty());
        app.saveProject(path);
        const json saved = loadJson(path);
        REQUIRE(saved["receiver_requirements"].is_object());
        CHECK(saved["receiver_requirements"]["invalid_configuration"] == true);
        CHECK(saved["receiver_requirements"]["diagnostic"].is_string());
        app.loadProject(path);
        CHECK(app.componentCount() == 2);
        CHECK_FALSE(app.testReceiverRequirementsState().config.has_value());
        CHECK_FALSE(app.testReceiverRequirementsState().invalid_reason.empty());
        std::filesystem::remove(path);
    }
}

TEST_CASE_METHOD(ImGuiFixture, "Finite stale receiver tone selector remains valid and unchanged",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_stale_tone_selector");
    const json requirements = {
        {"band_start_hz", 1.0e9},
        {"band_stop_hz", 2.0e9},
        {"measurement_conditions", json{{"output_reference_tone_frequency_hz", 3.0e9}}}};
    savedProjectWithRequirements(path, requirements);
    RfSimulatorApp app;
    app.loadProject(path);
    REQUIRE(app.testReceiverRequirementsState().config.has_value());
    CHECK(app.testReceiverRequirementsState().invalid_reason.empty());
    REQUIRE(app.testReceiverRequirementsState()
                .config->measurement_conditions.output_reference_tone_frequency_Hz.has_value());
    CHECK(*app.testReceiverRequirementsState()
               .config->measurement_conditions.output_reference_tone_frequency_Hz == 3.0e9);
    app.saveProject(path);
    CHECK(loadJson(path)["receiver_requirements"]["measurement_conditions"]
                       ["output_reference_tone_frequency_hz"] == 3.0e9);
    app.loadProject(path);
    REQUIRE(app.testReceiverRequirementsState().config.has_value());
    CHECK(*app.testReceiverRequirementsState()
               .config->measurement_conditions.output_reference_tone_frequency_Hz == 3.0e9);
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "Non-finite persisted receiver limits are invalid",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_non_finite_limit");
    const auto project = savedProjectWithRequirements(path, validRequirements);
    auto contents = project.dump(2);
    const auto field = contents.find("\"band_start_hz\": ");
    REQUIRE(field != std::string::npos);
    const auto value_start = contents.find(':', field) + 1;
    const auto value_end = contents.find(',', value_start);
    REQUIRE(value_end != std::string::npos);
    contents.replace(value_start, value_end - value_start, " 1e400");
    {
        std::ofstream output(path);
        output << contents;
    }
    RfSimulatorApp app;
    app.loadProject(path);
    CHECK(app.m_current_project_path == path);
    CHECK(app.componentCount() == 2);
    CHECK_FALSE(app.testReceiverRequirementsState().config.has_value());
    CHECK_FALSE(app.testReceiverRequirementsState().invalid_reason.empty());
    std::filesystem::remove(path);
}
