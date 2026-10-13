#include "agent_host.h"
#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "test_temp_paths.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>

namespace {
namespace fs = std::filesystem;
using nlohmann::json;
using nlohmann::ordered_json;

struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
        ImGui::GetIO().DisplaySize = ImVec2(1200.0f, 800.0f);
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char *pixels = nullptr;
        int width = 0;
        int height = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }

    ~ImGuiFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

struct AppFixture : ImGuiFixture {
    RfSimulatorApp app;
    fs::path temp_dir =
        fs::temp_directory_path() / ("agent_position_read_" + test_temp_paths::processTag());

    AppFixture() { fs::create_directories(temp_dir); }

    ~AppFixture() {
        std::error_code error;
        fs::remove_all(temp_dir, error);
    }
};
} // namespace

TEST_CASE_METHOD(AppFixture,
                 "circuit_get reads and saves a GUI-added position before its first draw",
                 "[agent_app][position]") {
    app.newProject();
    ImNodes::EditorContextSet(app.testGraphWidget().context());
    ImNodes::EditorContextResetPanning(ImVec2(0.0f, 0.0f));
    const ImVec2 expected_position(321.0f, 123.0f);

    IComponentEngine *component = app.testAddComponentAtPosition("generator", expected_position);
    REQUIRE(component != nullptr);

    const AgentToolResult result = app.testAgentCall("circuit_get", ordered_json::object());
    REQUIRE_FALSE(result.is_error);
    const auto &position = result.structured.at("components").at(0).at("position");
    CHECK(position.at("x").get<double>() == Catch::Approx(expected_position.x));
    CHECK(position.at("y").get<double>() == Catch::Approx(expected_position.y));

    const fs::path saved_path = temp_dir / "canvas_added_position.rfsim";
    app.saveProject(saved_path.string());
    std::ifstream saved_file(saved_path);
    REQUIRE(saved_file.good());
    const json saved_project = json::parse(saved_file);
    const auto &saved_position = saved_project.at("components").at(0).at("pos");
    CHECK(saved_position.at("x").get<double>() == Catch::Approx(expected_position.x));
    CHECK(saved_position.at("y").get<double>() == Catch::Approx(expected_position.y));
}

TEST_CASE_METHOD(AppFixture,
                 "circuit_get reads and saves a duplicated position before its first draw",
                 "[agent_app][position]") {
    app.newProject();
    ImNodes::EditorContextSet(app.testGraphWidget().context());
    ImNodes::EditorContextResetPanning(ImVec2(0.0f, 0.0f));
    const ImVec2 source_position(210.0f, 75.0f);
    const ImVec2 expected_position(source_position.x + 40.0f, source_position.y + 40.0f);

    IComponentEngine *source = app.testAddComponentAtPosition("generator", source_position);
    REQUIRE(source != nullptr);
    app.testGraphWidget().setNodeEditorSpacePosition(source->graphNodeId(), source_position);

    IComponentEngine *duplicate = app.testDuplicateComponent(source->graphNodeId());
    REQUIRE(duplicate != nullptr);

    const AgentToolResult result = app.testAgentCall("circuit_get", ordered_json::object());
    REQUIRE_FALSE(result.is_error);
    ordered_json duplicate_position;
    bool found_duplicate = false;
    for (const auto &entry : result.structured.at("components")) {
        if (entry.at("id").get<int>() == duplicate->id()) {
            duplicate_position = entry.at("position");
            found_duplicate = true;
            break;
        }
    }
    REQUIRE(found_duplicate);
    CHECK(duplicate_position.at("x").get<double>() == Catch::Approx(expected_position.x));
    CHECK(duplicate_position.at("y").get<double>() == Catch::Approx(expected_position.y));

    const fs::path saved_path = temp_dir / "duplicated_position.rfsim";
    app.saveProject(saved_path.string());
    std::ifstream saved_file(saved_path);
    REQUIRE(saved_file.good());
    const json saved_project = json::parse(saved_file);
    const auto &saved_components = saved_project.at("components");
    REQUIRE(saved_components.size() == 2);
    const auto &saved_duplicate_position = saved_components.at(1).at("pos");
    CHECK(saved_duplicate_position.at("x").get<double>() == Catch::Approx(expected_position.x));
    CHECK(saved_duplicate_position.at("y").get<double>() == Catch::Approx(expected_position.y));
}

TEST_CASE_METHOD(AppFixture, "Pan-aware position round trips and material moves",
                 "[agent_app][position]") {
    const auto verify_round_trip = [&](ImVec2 pan, bool check_scaled_tolerance) {
        app.newProject();
        ImNodes::EditorContextSet(app.testGraphWidget().context());
        ImNodes::EditorContextResetPanning(pan);

        IComponentEngine *component =
            app.testAddComponentAtPosition("generator", ImVec2(0.0f, 0.0f));
        REQUIRE(component != nullptr);
        const AgentToolResult before = app.testAgentCall("circuit_get", ordered_json::object());
        REQUIRE_FALSE(before.is_error);
        const auto revision_before = before.structured.at("revision").get<std::uint64_t>();
        const std::size_t checkpoints_before = app.testAgentHost().checkpoints().size();

        const auto move_to_position = [&](const ordered_json &position) {
            return app.testAgentCall(
                "circuit_edit",
                ordered_json{{"epoch", app.testProjectEpoch()},
                             {"ops", ordered_json::array({{{"op", "move"},
                                                           {"component", component->id()},
                                                           {"position", position}}})}});
        };

        const ordered_json target{{"x", 0.1}, {"y", 12.0}};
        const AgentToolResult first_move = move_to_position(target);
        REQUIRE_FALSE(first_move.is_error);
        const auto revision_after_first = first_move.structured.at("revision").get<std::uint64_t>();
        CHECK(revision_after_first == revision_before + 1);
        const std::size_t checkpoints_after_first = app.testAgentHost().checkpoints().size();
        CHECK(checkpoints_after_first == checkpoints_before + 1);

        const AgentToolResult after_first_move =
            app.testAgentCall("circuit_get", ordered_json::object());
        REQUIRE_FALSE(after_first_move.is_error);
        const ordered_json readback =
            after_first_move.structured.at("components").at(0).at("position");
        CHECK(readback.at("x").get<double>() ==
              Catch::Approx(target.at("x").get<double>()).margin(0.00005));
        CHECK(readback.at("y").get<double>() ==
              Catch::Approx(target.at("y").get<double>()).margin(0.00005));

        const AgentToolResult round_trip_move = move_to_position(readback);
        REQUIRE_FALSE(round_trip_move.is_error);
        REQUIRE(round_trip_move.structured.at("applied").at(0).at("unchanged") == true);
        CHECK(round_trip_move.structured.at("revision") == revision_after_first);
        CHECK(app.testAgentHost().checkpoints().size() == checkpoints_after_first);

        if (check_scaled_tolerance) {
            ordered_json near_position = readback;
            near_position["x"] = readback.at("x").get<double>() + 0.00005;
            const AgentToolResult near_move = move_to_position(near_position);
            REQUIRE_FALSE(near_move.is_error);
            REQUIRE(near_move.structured.at("applied").at(0).at("unchanged") == true);
            CHECK(near_move.structured.at("revision") == revision_after_first);
            CHECK(app.testAgentHost().checkpoints().size() == checkpoints_after_first);
        }

        ordered_json material_position = readback;
        material_position["x"] = readback.at("x").get<double>() + 0.01;
        const AgentToolResult material_move = move_to_position(material_position);
        REQUIRE_FALSE(material_move.is_error);
        CHECK_FALSE(material_move.structured.at("applied").at(0).contains("unchanged"));
        CHECK(material_move.structured.at("revision") == revision_after_first + 1);
        CHECK(app.testAgentHost().checkpoints().size() == checkpoints_after_first + 1);
    };

    SECTION("fractional negative canvas pan") {
        verify_round_trip(ImVec2(-13.256257f, 0.5f), false);
    }
    SECTION("large canvas pan") { verify_round_trip(ImVec2(-1000.0f, 500.0f), true); }
    SECTION("fractional positive canvas pan") {
        verify_round_trip(ImVec2(13.256257f, -0.5f), false);
    }
}

TEST_CASE_METHOD(AppFixture, "A collapsed member moved by the agent keeps its position when saved",
                 "[agent_app][position][collapsed]") {
    app.newProject();
    ImNodes::EditorContextSet(app.testGraphWidget().context());
    ImNodes::EditorContextResetPanning(ImVec2(-13.256257f, 0.5f));

    IComponentEngine *moved_component =
        app.testAddComponentAtPosition("generator", ImVec2(80.0f, 40.0f));
    IComponentEngine *other_component =
        app.testAddComponentAtPosition("generator", ImVec2(240.0f, 40.0f));
    REQUIRE(moved_component != nullptr);
    REQUIRE(other_component != nullptr);

    bool open = true;
    const auto draw_editor = [&]() {
        ImGui::NewFrame();
        app.testGraphWidget().draw("Node Editor Test", &open);
        ImGui::Render();
    };
    draw_editor();

    const int group_id = app.testCreateGroup(
        "position group", {moved_component->graphNodeId(), other_component->graphNodeId()});
    REQUIRE(group_id > 0);
    REQUIRE(app.testSetGroupCollapsed(group_id, true));
    draw_editor();

    const ImVec2 expected_position(350.0f, 200.0f);
    const AgentToolResult move = app.testAgentCall(
        "circuit_edit",
        ordered_json{{"epoch", app.testProjectEpoch()},
                     {"ops", ordered_json::array(
                                 {{{"op", "move"},
                                   {"component", moved_component->id()},
                                   {"position",
                                    {{"x", expected_position.x}, {"y", expected_position.y}}}}})}});
    REQUIRE_FALSE(move.is_error);

    const AgentToolResult read = app.testAgentCall("circuit_get", ordered_json::object());
    REQUIRE_FALSE(read.is_error);
    ordered_json moved_position;
    bool found_moved_component = false;
    for (const auto &entry : read.structured.at("components")) {
        if (entry.at("id").get<int>() == moved_component->id()) {
            moved_position = entry.at("position");
            found_moved_component = true;
            break;
        }
    }
    REQUIRE(found_moved_component);
    CHECK(moved_position.at("x").get<double>() == Catch::Approx(expected_position.x));
    CHECK(moved_position.at("y").get<double>() == Catch::Approx(expected_position.y));
    const std::uint64_t revision_after_move = move.structured.at("revision").get<std::uint64_t>();
    const std::size_t checkpoints_after_move = app.testAgentHost().checkpoints().size();
    const AgentToolResult round_trip_move = app.testAgentCall(
        "circuit_edit",
        ordered_json{{"epoch", app.testProjectEpoch()},
                     {"ops", ordered_json::array({{{"op", "move"},
                                                   {"component", moved_component->id()},
                                                   {"position", moved_position}}})}});
    REQUIRE_FALSE(round_trip_move.is_error);
    CHECK(round_trip_move.structured.at("applied").at(0).at("unchanged") == true);
    CHECK(round_trip_move.structured.at("revision") == revision_after_move);
    CHECK(app.testAgentHost().checkpoints().size() == checkpoints_after_move);

    const fs::path saved_path = temp_dir / "collapsed_member_position.rfsim";
    app.saveProject(saved_path.string());
    std::ifstream saved_file(saved_path);
    REQUIRE(saved_file.good());
    const json saved_project = json::parse(saved_file);
    REQUIRE(saved_project.at("components").size() == 2);
    const auto &saved_position = saved_project.at("components").at(0).at("pos");
    CHECK(saved_position.at("x").get<double>() == Catch::Approx(expected_position.x));
    CHECK(saved_position.at("y").get<double>() == Catch::Approx(expected_position.y));
}
