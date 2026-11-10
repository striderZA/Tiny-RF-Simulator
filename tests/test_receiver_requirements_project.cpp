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

TEST_CASE_METHOD(ImGuiFixture, "Receiver requirements load a legacy object as one Default mode",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_valid");
    savedProjectWithRequirements(path, validRequirements);
    RfSimulatorApp app;
    app.loadProject(path);
    auto &set = app.testReceiverRequirements();
    REQUIRE(set.modes.size() == 1);
    CHECK(set.modes[0].name == "Default");
    CHECK(set.selected == 0);
    REQUIRE(set.modes[0].config.has_value());
    CHECK(set.modes[0].config->gain.has_value());
    CHECK(set.modes[0].config->gain->minimum_dB == 10.0);
    CHECK(set.modes[0].config->gain->maximum_dB == 20.0);
    CHECK(set.modes[0].config->nf_max_dB == 5.0);
    CHECK_FALSE(set.modes[0].config->measurement_conditions.input_level_dBm.has_value());
    CHECK(app.componentCount() == 2);

    const auto loaded_state = app.testReceiverRequirementsState();
    REQUIRE(loaded_state.config.has_value());
    CHECK(loaded_state.invalid_reason.empty());

    app.saveProject(path);
    const json saved = loadJson(path);
    REQUIRE(saved.contains("receiver_requirements"));
    const auto &requirements = saved["receiver_requirements"];
    CHECK(requirements["format"] == 3);
    REQUIRE(requirements["modes"].is_array());
    REQUIRE(requirements["modes"].size() == 1);
    CHECK(requirements["modes"][0]["name"] == "Default");
    CHECK(requirements["modes"][0]["config"] == validRequirements);
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "Receiver requirements round-trip two modes and the selected mode",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_modes_round_trip");
    ReceiverRequirementsSet original;
    ReceiverRequirementsMode high;
    high.name = "Band 1 High Gain";
    ReceiverRequirementsConfig high_config;
    high_config.band_start_Hz = 5.0e6;
    high_config.band_stop_Hz = 10.0e6;
    high_config.gain = ReceiverGainLimits{10.0, 20.0};
    high.config = high_config;
    ReceiverRequirementsMode low;
    low.name = "Band 1 Low Gain";
    ReceiverRequirementsConfig low_config = high_config;
    low_config.gain = ReceiverGainLimits{0.0, 5.0};
    low_config.measurement_conditions.input_level_dBm = -30.0;
    low.config = low_config;
    high.id = 1;
    low.id = 2;
    original.modes = {high, low};
    original.selected = 1;
    original.next_mode_id = 3;
    {
        RfSimulatorApp app;
        app.testReceiverRequirements() = original;
        app.saveProject(path);
    }
    RfSimulatorApp app;
    app.loadProject(path);
    CHECK(app.testReceiverRequirements() == original);
    const auto loaded_state = app.testReceiverRequirementsState();
    REQUIRE(loaded_state.config.has_value());
    REQUIRE(loaded_state.config->gain.has_value());
    CHECK(loaded_state.config->gain->minimum_dB == 0.0);
    CHECK(loaded_state.config->measurement_conditions.input_level_dBm == -30.0);
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "Receiver requirements with no modes are not saved",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_no_modes");
    RfSimulatorApp app;
    app.saveProject(path);
    const json saved = loadJson(path);
    CHECK_FALSE(saved.contains("receiver_requirements"));
    app.loadProject(path);
    CHECK(app.testReceiverRequirements().modes.empty());
    CHECK(app.testReceiverRequirementsState().invalid_reason.empty());
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "Receiver requirements format 2 file saves as format 3 with ids",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_format2_to_3");
    savedProjectWithRequirements(path, json{{"format", 2},
                                            {"selected_mode", 1},
                                            {"modes",
                                             {{{"name", "HG"}, {"config", nullptr}},
                                              {{"name", "LG"}, {"config", nullptr}}}}});
    RfSimulatorApp app;
    app.loadProject(path);
    const auto &set = app.testReceiverRequirements();
    REQUIRE(set.modes.size() == 2);
    CHECK(set.modes[0].id == 1);
    CHECK(set.modes[1].id == 2);
    CHECK(set.next_mode_id == 3);
    app.saveProject(path);
    const json saved = loadJson(path);
    CHECK(saved["receiver_requirements"]["format"] == 3);
    CHECK(saved["receiver_requirements"]["next_mode_id"] == 3);
    CHECK(saved["receiver_requirements"]["modes"][1]["id"] == 2);
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "Receiver requirements persist optional metrics and conditions",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_optional");
    RfSimulatorApp app;
    ReceiverRequirementsConfig config;
    config.band_start_Hz = 1.0e9;
    config.band_stop_Hz = 2.0e9;
    config.gain = ReceiverGainLimits{10.0, 20.0};
    config.nf_max_dB = 5.0;
    config.output_power = ReceiverOutputPowerLimits{-30.0, 0.0};
    config.iip3_min_dBm = 10.0;
    config.measurement_conditions.input_level_dBm = -30.0;
    config.measurement_conditions.iip3 = ReceiverIIP3TestSettings{1.0e6, -30.0, -20.0, 5.0};
    app.testSetSingleModeRequirements(config);
    app.saveProject(path);

    const auto saved = loadJson(path);
    const auto &requirements = saved["receiver_requirements"]["modes"][0]["config"];
    CHECK(requirements["output_power_min_dbm"] == -30.0);
    CHECK(requirements["output_power_max_dbm"] == 0.0);
    CHECK(requirements["iip3_min_dbm"] == 10.0);
    CHECK(requirements["measurement_conditions"]["input_level_dbm"] == -30.0);
    CHECK(requirements["measurement_conditions"]["iip3"]["tone_spacing_hz"] == 1.0e6);
    CHECK(requirements["measurement_conditions"]["iip3"]["input_start_dbm"] == -30.0);
    CHECK(requirements["measurement_conditions"]["iip3"]["input_stop_dbm"] == -20.0);
    CHECK(requirements["measurement_conditions"]["iip3"]["input_step_db"] == 5.0);
    app.loadProject(path);
    const auto state = app.testReceiverRequirementsState();
    REQUIRE(state.config.has_value());
    REQUIRE(state.config->output_power.has_value());
    CHECK(state.config->output_power->minimum_dBm == -30.0);
    CHECK(state.config->output_power->maximum_dBm == 0.0);
    CHECK(state.config->iip3_min_dBm == config.iip3_min_dBm);
    REQUIRE(state.config->measurement_conditions.iip3.has_value());
    CHECK(state.config->measurement_conditions.iip3->tone_spacing_Hz == 1.0e6);
    CHECK(state.config->measurement_conditions.input_level_dBm == -30.0);
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "Receiver requirements persist gain-only and NF-only configs",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_partial");
    RfSimulatorApp app;
    ReceiverRequirementsConfig config;
    config.band_start_Hz = 1.0e9;
    config.band_stop_Hz = 2.0e9;
    config.gain = ReceiverGainLimits{10.0, 20.0};
    app.testSetSingleModeRequirements(config);
    app.saveProject(path);
    app.loadProject(path);
    {
        const auto state = app.testReceiverRequirementsState();
        REQUIRE(state.config.has_value());
        REQUIRE(state.config->gain.has_value());
        CHECK(state.config->gain->minimum_dB == 10.0);
        CHECK(state.config->gain->maximum_dB == 20.0);
        CHECK_FALSE(state.config->nf_max_dB.has_value());
    }
    CHECK_FALSE(
        loadJson(path)["receiver_requirements"]["modes"][0]["config"].contains("nf_max_db"));

    config.gain.reset();
    config.nf_max_dB = 5.0;
    app.testSetSingleModeRequirements(config);
    app.saveProject(path);
    app.loadProject(path);
    {
        const auto state = app.testReceiverRequirementsState();
        REQUIRE(state.config.has_value());
        CHECK_FALSE(state.config->gain.has_value());
        CHECK(state.config->nf_max_dB == 5.0);
    }
    CHECK_FALSE(
        loadJson(path)["receiver_requirements"]["modes"][0]["config"].contains("gain_min_db"));
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
             {"measurement_conditions", json{{"input_level_dbm", -30.0},
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
                  .config->measurement_conditions.input_level_dBm.has_value());
        REQUIRE(
            app.testReceiverRequirementsState().config->measurement_conditions.iip3.has_value());
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
        {"half_gain",
         json{{"band_start_hz", 1.0e9}, {"band_stop_hz", 2.0e9}, {"gain_min_db", 10.0}}},
        {"half_output_power",
         json{{"band_start_hz", 1.0e9}, {"band_stop_hz", 2.0e9}, {"output_power_min_dbm", -30.0}}},
        {"wrong_nf", json{{"band_start_hz", 1.0e9}, {"band_stop_hz", 2.0e9}, {"nf_max_db", false}}},
        {"wrong_iip3_limit",
         json{{"band_start_hz", 1.0e9}, {"band_stop_hz", 2.0e9}, {"iip3_min_dbm", "10"}}},
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
        {"iip3_not_object", json{{"band_start_hz", 1.0e9},
                                 {"band_stop_hz", 2.0e9},
                                 {"measurement_conditions", json{{"iip3", 4}}}}},
        {"iip3_missing_nested_field",
         json{{"band_start_hz", 1.0e9},
              {"band_stop_hz", 2.0e9},
              {"measurement_conditions", json{{"iip3", json{{"tone_spacing_hz", 1.0e6},
                                                            {"input_start_dbm", -30.0},
                                                            {"input_stop_dbm", -20.0}}}}}}},
        {"malformed_iip3_while_disabled",
         json{{"band_start_hz", 1.0e9},
              {"band_stop_hz", 2.0e9},
              {"measurement_conditions", json{{"iip3", json{{"tone_spacing_hz", 1.0e6},
                                                            {"input_start_dbm", -30.0},
                                                            {"input_stop_dbm", -20.0},
                                                            {"input_step_db", "5"}}}}}}},
        {"enabled_iip3_without_settings",
         json{{"band_start_hz", 1.0e9}, {"band_stop_hz", 2.0e9}, {"iip3_min_dbm", 10.0}}},
        {"enabled_iip3_with_invalid_settings",
         json{{"band_start_hz", 1.0e9},
              {"band_stop_hz", 2.0e9},
              {"iip3_min_dbm", 10.0},
              {"measurement_conditions", json{{"iip3", json{{"tone_spacing_hz", 0.0},
                                                            {"input_start_dbm", -30.0},
                                                            {"input_stop_dbm", -20.0},
                                                            {"input_step_db", 5.0}}}}}}},
        {"iip3_sweep_over_101_levels",
         json{{"band_start_hz", 1.0e9},
              {"band_stop_hz", 2.0e9},
              {"iip3_min_dbm", 10.0},
              {"measurement_conditions", json{{"iip3", json{{"tone_spacing_hz", 1.0e6},
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

TEST_CASE_METHOD(ImGuiFixture,
                 "Receiver requirements drop the legacy reference tone and keep the input level",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_legacy_tone_input_level");
    auto requirements = validRequirements;
    requirements["output_power_min_dbm"] = -40.0;
    requirements["output_power_max_dbm"] = 0.0;
    requirements["measurement_conditions"] = json{{"output_reference_tone_frequency_hz", 1.0e9}};
    savedProjectWithRequirements(path, requirements);
    {
        RfSimulatorApp app;
        app.loadProject(path);
        const auto legacy_state = app.testReceiverRequirementsState();
        REQUIRE(legacy_state.config.has_value());
        const auto &config = *legacy_state.config;
        REQUIRE(config.output_power.has_value());
        CHECK_FALSE(config.measurement_conditions.input_level_dBm.has_value());
        // No silent fallback: a migrated output-power limit without an input
        // level stays Incomplete instead of borrowing a generator tone.
        // The engine returns no output-power samples without an input level.
        const auto evaluation =
            evaluateReceiverRequirements(legacy_state, 1.0e9, 2.0e9, {1.5e9}, {0.0}, {0.0}, {}, {});
        CHECK(evaluation.output_power.status == ReceiverRequirementStatus::Incomplete);
        app.saveProject(path);
    }
    {
        const json saved = loadJson(path);
        REQUIRE(saved.contains("receiver_requirements"));
        CHECK(saved["receiver_requirements"].dump().find("output_reference_tone_frequency_hz") ==
              std::string::npos);
    }
    {
        auto project = loadJson(path);
        project["receiver_requirements"]["modes"][0]["config"]["measurement_conditions"] =
            json{{"input_level_dbm", -30.0}};
        saveJson(path, project);
    }
    {
        RfSimulatorApp app;
        app.loadProject(path);
        REQUIRE(app.testReceiverRequirementsState().config.has_value());
        CHECK(app.testReceiverRequirementsState().config->measurement_conditions.input_level_dBm ==
              -30.0);
        app.saveProject(path);
    }
    {
        const json saved = loadJson(path);
        CHECK(saved["receiver_requirements"]["modes"][0]["config"]["measurement_conditions"]
                   ["input_level_dbm"] == -30.0);
    }
    {
        auto project = loadJson(path);
        project["receiver_requirements"]["modes"][0]["config"]["measurement_conditions"] =
            json{{"input_level_dbm", "bad"}};
        saveJson(path, project);
    }
    {
        RfSimulatorApp app;
        app.loadProject(path);
        CHECK_FALSE(app.testReceiverRequirementsState().config.has_value());
        CHECK(app.testReceiverRequirementsState().invalid_reason ==
              "Mode 1: Input level must be a number.");
    }
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "Non-finite persisted receiver limits are invalid",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_non_finite_limit");
    auto project = savedProjectWithRequirements(path, validRequirements);
    project["components"][0]["part_number"] = "1e400";
    saveJson(path, project);
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
    app.saveProject(path);
    CHECK(loadJson(path)["components"][0]["part_number"] == "1e400");
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture,
                 "Overflowing component position is rejected alongside receiver overflow",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_component_position_overflow");
    json project = savedProjectWithRequirements(path, validRequirements);
    REQUIRE(!project["components"].empty());
    project["components"][0]["pos"]["x"] = 123.0;
    saveJson(path, project);

    std::string source;
    {
        std::ifstream input(path);
        source.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    const auto receiver_key = source.find("\"receiver_requirements\"");
    REQUIRE(receiver_key != std::string::npos);
    const auto band_key = source.find("\"band_start_hz\"", receiver_key);
    REQUIRE(band_key != std::string::npos);
    auto value_start = source.find_first_not_of(" \t\r\n", source.find(':', band_key) + 1);
    REQUIRE(value_start != std::string::npos);
    auto value_end = source.find_first_of(",}", value_start);
    REQUIRE(value_end != std::string::npos);
    source.replace(value_start, value_end - value_start, "1e400");
    const auto pos_key = source.find("\"pos\"");
    REQUIRE(pos_key != std::string::npos);
    const auto x_key = source.find("\"x\"", pos_key);
    REQUIRE(x_key != std::string::npos);
    const auto colon = source.find(':', x_key);
    REQUIRE(colon != std::string::npos);
    value_start = source.find_first_not_of(" \t\r\n", colon + 1);
    REQUIRE(value_start != std::string::npos);
    value_end = source.find_first_of(",}", value_start);
    REQUIRE(value_end != std::string::npos);
    source.replace(value_start, value_end - value_start, "1e400");
    {
        std::ofstream output(path, std::ios::trunc);
        output << source;
    }

    RfSimulatorApp app;
    app.loadProject(path);
    const bool loaded = app.m_current_project_path == path;
    CHECK_FALSE(loaded);
    if (loaded)
        app.saveProject(path);

    std::string after;
    {
        std::ifstream after_input(path);
        after.assign(std::istreambuf_iterator<char>(after_input), std::istreambuf_iterator<char>());
    }
    CHECK(after == source);
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "Nested receiver overflow remains field-invalid without parse loss",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_nested_overflow");
    const json requirements = {
        {"band_start_hz", 1.0e9},
        {"band_stop_hz", 2.0e9},
        {"iip3_min_dbm", 10.0},
        {"measurement_conditions", json{{"diagnostic", "}\"1e400"},
                                        {"iip3", json{{"tone_spacing_hz", 1.0e6},
                                                      {"input_start_dbm", -30.0},
                                                      {"input_stop_dbm", -20.0},
                                                      {"input_step_db", 5.0}}}}}};
    auto project = savedProjectWithRequirements(path, requirements);
    auto contents = project.dump(2);
    const auto field = contents.find("\"input_step_db\": ");
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
    CHECK_FALSE(app.testReceiverRequirementsState().config.has_value());
    CHECK_FALSE(app.testReceiverRequirementsState().invalid_reason.empty());
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "BOM-prefixed receiver overflow remains field-invalid",
                 "[receiver_requirements][project]") {
    const auto path = tempPath("_bom_non_finite_limit");
    const auto project = savedProjectWithRequirements(path, validRequirements);
    auto contents = project.dump(2);
    const auto field = contents.find("\"band_start_hz\": ");
    REQUIRE(field != std::string::npos);
    const auto value_start = contents.find(':', field) + 1;
    const auto value_end = contents.find(',', value_start);
    REQUIRE(value_end != std::string::npos);
    contents.replace(value_start, value_end - value_start, " 1e400");
    contents.insert(0, "\xEF\xBB\xBF");
    {
        std::ofstream output(path, std::ios::trunc);
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
