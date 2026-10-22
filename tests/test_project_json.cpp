#include "amplifier_engine.h"
#include "app.h"
#include "ideal_filter_engine.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "project_serializer.h"
#include "signal_generator_engine.h"
#include "test_temp_paths.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

using nlohmann::json;

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

namespace {
std::filesystem::path tempPath(const std::string &suffix) {
    return std::filesystem::temp_directory_path() /
           ("test_project_json_" + test_temp_paths::processTag() + suffix + ".rfsim");
}

std::string readBytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

// The app saves these panels to the shared app.ini when it is destroyed. Capture them
// on construction and restore them when the scope ends, so a case that changes them
// leaves app.ini as it found it. Declare it after the app: destructors run in reverse
// order, so the restore runs before the app saves, including when a REQUIRE throws.
class ScopedWindowState {
  public:
    explicit ScopedWindowState(RfSimulatorApp &app)
        : m_app(app), m_log(app.m_show_log), m_spectrum(app.m_show_spectrum),
          m_properties(app.m_show_properties), m_node_editor(app.m_show_node_editor) {}
    ~ScopedWindowState() {
        m_app.m_show_log = m_log;
        m_app.m_show_spectrum = m_spectrum;
        m_app.m_show_properties = m_properties;
        m_app.m_show_node_editor = m_node_editor;
    }
    ScopedWindowState(const ScopedWindowState &) = delete;
    ScopedWindowState &operator=(const ScopedWindowState &) = delete;

  private:
    RfSimulatorApp &m_app;
    bool m_log;
    bool m_spectrum;
    bool m_properties;
    bool m_node_editor;
};
} // namespace

TEST_CASE_METHOD(ImGuiFixture, "save writes toJson plus the project name", "[project_json]") {
    const auto path = tempPath("_save");
    RfSimulatorApp app;
    auto &generator =
        static_cast<SignalGeneratorEngine &>(*app.testCreateComponent("generator", 10001));
    generator.addTone(123.0e6, -17.0);

    json expected = app.testProjectSerializer().toJson({path.parent_path(), true});
    expected["name"] = path.stem().string();
    app.saveProject(path.string());

    CHECK(readBytes(path) == expected.dump(2));
    std::filesystem::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "Checkpoint snapshots keep S-parameter paths verbatim",
                 "[project_json]") {
    RfSimulatorApp app;
    app.newProject(); // The app constructor seeds a default demo circuit.
    const std::string source_path =
        (std::filesystem::path(PROJECT_SOURCE_DIR) /
         "component_data/amplifiers/adm-3844psm/ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p")
            .string();
    auto &amplifier = static_cast<AmplifierEngine &>(*app.testCreateComponent("amplifier", 10001));
    amplifier.setSParamFilepath(source_path);
    REQUIRE(amplifier.sparamMode());

    const json snapshot = app.testProjectSerializer().toJson({std::nullopt, false});
    REQUIRE(snapshot.at("components").size() == 1);
    CHECK(snapshot.at("components").at(0).at("params").at("sparam_filepath") == source_path);

    app.newProject();
    REQUIRE(app.componentCount() == 0);
    REQUIRE(app.testProjectSerializer().fromJson(snapshot, {std::nullopt, false}, "checkpoint"));

    const auto restored = app.testComponents().byType<AmplifierEngine>();
    REQUIRE(restored.size() == 1);
    CHECK(restored[0]->sparamMode());
    CHECK(restored[0]->sparamFilepath() == source_path);
}

TEST_CASE_METHOD(ImGuiFixture, "fromJson can leave window state alone", "[project_json]") {
    RfSimulatorApp app;
    ScopedWindowState restore_window_state(app);
    app.m_show_log = true;
    app.m_show_spectrum = false;
    app.m_show_properties = false;
    app.m_show_node_editor = true;

    json snapshot = app.testProjectSerializer().toJson({std::nullopt, false});
    snapshot["window_state"] = {
        {"log", false}, {"spectrum_analyzer", true}, {"properties", true}, {"node_editor", false}};

    REQUIRE(app.testProjectSerializer().fromJson(snapshot, {std::nullopt, false}, "checkpoint"));
    CHECK(app.m_show_log);
    CHECK_FALSE(app.m_show_spectrum);
    CHECK_FALSE(app.m_show_properties);
    CHECK(app.m_show_node_editor);
}

TEST_CASE_METHOD(ImGuiFixture, "fromJson ignores window-state shape when disabled",
                 "[project_json]") {
    RfSimulatorApp app;
    ScopedWindowState restore_window_state(app);
    app.newProject();
    app.m_show_log = false;

    json snapshot = app.testProjectSerializer().toJson({std::nullopt, false});
    snapshot["window_state"] = 7;

    REQUIRE(app.testProjectSerializer().fromJson(snapshot, {std::nullopt, false}, "checkpoint"));
    CHECK_FALSE(app.m_show_log);
}

TEST_CASE_METHOD(ImGuiFixture, "fromJson replaces the project like a load", "[project_json]") {
    RfSimulatorApp app;
    app.newProject(); // The app constructor seeds a default demo circuit.
    auto &old_generator =
        static_cast<SignalGeneratorEngine &>(*app.testCreateComponent("generator", 10001));
    old_generator.addTone(80.0e6, -20.0);

    json snapshot = app.testProjectSerializer().toJson({std::nullopt, false});
    snapshot["components"].at(0)["params"]["tones"] =
        json::array({json{{"freq_Hz", 250.0e6}, {"power_dBm", -9.0}, {"phase_deg", 37.0}}});

    app.newProject();
    auto &old_amplifier =
        static_cast<AmplifierEngine &>(*app.testCreateComponent("amplifier", 10002));
    (void)old_amplifier;
    const auto before = app.testProjectSerializer().toJson({std::nullopt, false});
    REQUIRE(before.at("components").size() == 1);
    CHECK(before.at("components").at(0).at("type") != snapshot.at("components").at(0).at("type"));
    const auto epoch_before = app.testProjectEpoch();
    REQUIRE(app.testProjectSerializer().fromJson(snapshot, {std::nullopt, false}, "checkpoint"));
    CHECK(app.testProjectEpoch() == epoch_before + 1);

    const auto restored = app.testComponents().byType<SignalGeneratorEngine>();
    REQUIRE(restored.size() == 1);
    CHECK(restored[0]->tones().size() == 1);
    CHECK(restored[0]->tones()[0].freq_Hz == 250.0e6);
    CHECK(restored[0]->tones()[0].power_dBm == -9.0);
    CHECK(app.testComponents().byType<AmplifierEngine>().empty());
    CHECK(app.componentCount() == 1);
}

TEST_CASE_METHOD(ImGuiFixture,
                 "Project load refuses an inverted ideal filter band and keeps the open project",
                 "[project_json]") {
    RfSimulatorApp app;
    app.newProject();
    app.testCreateComponent("generator", 10101);
    const auto before = app.testProjectSerializer().toJson({std::nullopt, false});
    const auto epoch_before = app.testProjectEpoch();

    // A file holding a valid generator and an inverted BPF. Skipping the filter would
    // drop it and its links from the next save, so the whole file must be refused.
    json snapshot = before;
    snapshot["components"].push_back(
        {{"type", "IdealFilter"},
         {"params", {{"filter_type", "BPF"}, {"fc_low_Hz", 200e6}, {"fc_high_Hz", 100e6}}}});
    CHECK_FALSE(
        app.testProjectSerializer().fromJson(snapshot, {std::nullopt, false}, "checkpoint"));

    CHECK_FALSE(app.testProjectSerializer().lastLoadReset());
    CHECK(app.testProjectEpoch() == epoch_before);
    CHECK(app.componentCount() == 1);
    CHECK(app.testProjectSerializer().toJson({std::nullopt, false}).at("components") ==
          before.at("components"));
    CHECK(app.testComponents().byType<IdealFilterEngine>().empty());
}

TEST_CASE_METHOD(ImGuiFixture,
                 "Project load refuses an inverted filter record under its registry key",
                 "[project_json]") {
    // The registry accepts "filter" as well as "IdealFilter" for the same descriptor. Both
    // spellings must hit the refusal, or the record is skipped and its links are lost.
    RfSimulatorApp app;
    app.newProject();
    app.testCreateComponent("generator", 10101);
    const auto before = app.testProjectSerializer().toJson({std::nullopt, false});

    json snapshot = before;
    snapshot["components"].push_back(
        {{"type", "filter"},
         {"params", {{"filter_type", "BPF"}, {"fc_low_Hz", 200e6}, {"fc_high_Hz", 100e6}}}});
    CHECK_FALSE(
        app.testProjectSerializer().fromJson(snapshot, {std::nullopt, false}, "checkpoint"));
    CHECK(app.componentCount() == 1);
    CHECK(app.testProjectSerializer().toJson({std::nullopt, false}).at("components") ==
          before.at("components"));
}
