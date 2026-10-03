#include "app.h"
#include "receiver_requirements.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "test_temp_paths.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

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
    project["receiver_requirements"] = validRequirements;
    saveJson(path, project);
    {
        RfSimulatorApp app;
        app.loadProject(path);
        CHECK(app.componentCount() == 2);
        app.newProject();
        app.saveProject(path);
    }
    const json saved = loadJson(path);
    CHECK_FALSE(saved.contains("receiver_requirements"));
    std::filesystem::remove(path);
}
