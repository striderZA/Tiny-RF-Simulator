#include "agent_host.h"
#include "app.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imnodes.h"
#include "implot.h"
#include "test_temp_paths.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
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

    fs::path makePath(const std::string &name) const { return temp_dir / name; }

    ~AppFixture() {
        std::error_code error;
        fs::remove_all(temp_dir, error);
    }
};
void runFrame(const std::function<void()> &fn) {
    ImGui::NewFrame();
    fn();
    ImGui::Render();
}

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

TEST_CASE_METHOD(AppFixture, "Agent component moves update the canvas and revert restores position",
                 "[agent_app]") {
    app.newProject();

    const AgentToolResult add = app.testAgentCall(
        "circuit_edit",
        ordered_json{{"epoch", app.testProjectEpoch()},
                     {"ops", ordered_json::array({{{"op", "add"},
                                                   {"type", "generator"},
                                                   {"position", {{"x", 20.0}, {"y", 30.0}}}}})}});
    REQUIRE_FALSE(add.is_error);

    const AgentToolResult before = app.testAgentCall("circuit_get", ordered_json::object());
    REQUIRE_FALSE(before.is_error);
    REQUIRE(before.structured.at("components").size() == 1);
    const int component_id = before.structured.at("components").at(0).at("id").get<int>();
    const auto components = app.testComponents().all();
    REQUIRE(components.size() == 1);
    const int graph_node_id = components.front()->graphNodeId();
    const ImVec2 original_position = app.testGraphWidget().editorPositionForSave(graph_node_id);
    const std::uint64_t revision = before.structured.at("revision").get<std::uint64_t>();
    const double target_x = static_cast<double>(original_position.x) + 220.0;
    const double target_y = static_cast<double>(original_position.y) + 45.0;

    const AgentToolResult move = app.testAgentCall(
        "circuit_edit",
        ordered_json{
            {"epoch", app.testProjectEpoch()},
            {"ops", ordered_json::array({{{"op", "move"},
                                          {"component", component_id},
                                          {"position", {{"x", target_x}, {"y", target_y}}}}})}});
    REQUIRE_FALSE(move.is_error);

    const AgentToolResult after = app.testAgentCall("circuit_get", ordered_json::object());
    REQUIRE_FALSE(after.is_error);
    CHECK(after.structured.at("components").at(0).at("position").at("x").get<double>() ==
          Catch::Approx(target_x));
    CHECK(after.structured.at("components").at(0).at("position").at("y").get<double>() ==
          Catch::Approx(target_y));
    CHECK(after.structured.at("revision") == revision + 1);
    CHECK(after.structured.at("dirty") == true);

    REQUIRE(app.testAgentHost().checkpoints().size() == 2);
    const std::uint64_t move_checkpoint = app.testAgentHost().checkpoints().back().id;
    app.revertAgentCheckpoint(move_checkpoint);

    const AgentToolResult restored = app.testAgentCall("circuit_get", ordered_json::object());
    REQUIRE_FALSE(restored.is_error);
    CHECK(restored.structured.at("components").at(0).at("position").at("x").get<double>() ==
          Catch::Approx(original_position.x));
    CHECK(restored.structured.at("components").at(0).at("position").at("y").get<double>() ==
          Catch::Approx(original_position.y));
    CHECK(restored.structured.at("dirty") == true);
}

TEST_CASE_METHOD(AppFixture, "Agent rejects moving a new component in the same batch",
                 "[agent_app]") {
    app.newProject();

    const AgentToolResult initial_add = app.testAgentCall(
        "circuit_edit",
        ordered_json{{"epoch", app.testProjectEpoch()},
                     {"ops", ordered_json::array({{{"op", "add"}, {"type", "generator"}}})}});
    REQUIRE_FALSE(initial_add.is_error);
    const int next_component_id =
        initial_add.structured.at("applied").at(0).at("component").get<int>() + 1;

    const AgentToolResult result = app.testAgentCall(
        "circuit_edit",
        ordered_json{{"epoch", app.testProjectEpoch()},
                     {"ops", ordered_json::array({{{"op", "add"}, {"type", "generator"}},
                                                  {{"op", "move"},
                                                   {"component", next_component_id},
                                                   {"position", {{"x", 30.0}, {"y", 40.0}}}}})}});

    REQUIRE(result.is_error);
    CHECK(result.structured.at("error").at("code") == "INVALID_ARGUMENT");
    CHECK(result.structured.at("error").at("details").at("path") == "/ops/1/component");
    REQUIRE(result.structured.at("applied").size() == 1);
    CHECK(result.structured.at("applied").at(0).at("component") == next_component_id);
}

TEST_CASE_METHOD(AppFixture, "Collapsed group members retain their positions after expansion",
                 "[agent_app][collapsed]") {
    app.newProject();
    const auto add = [&](double x, double y) {
        return app.testAgentCall(
            "circuit_edit",
            ordered_json{{"epoch", app.testProjectEpoch()},
                         {"ops", ordered_json::array({{{"op", "add"},
                                                       {"type", "generator"},
                                                       {"position", {{"x", x}, {"y", y}}}}})}});
    };
    REQUIRE_FALSE(add(20.0, 30.0).is_error);
    REQUIRE_FALSE(add(260.0, 140.0).is_error);

    const auto components = app.testComponents().all();
    REQUIRE(components.size() == 2);
    const int first_node = components[0]->graphNodeId();
    const int second_node = components[1]->graphNodeId();
    const int group_id = app.testCreateGroup("Position test", {first_node, second_node});
    REQUIRE(group_id >= 0);
    REQUIRE(app.testSetGroupCollapsed(group_id, false));

    bool open = true;
    const auto draw_canvas = [&]() {
        runFrame([&]() { app.testGraphWidget().draw("Node Editor Test", &open); });
    };
    draw_canvas();
    const ImVec2 first_before = app.testGraphWidget().editorPositionForSave(first_node);
    const ImVec2 second_before = app.testGraphWidget().editorPositionForSave(second_node);

    REQUIRE(app.testSetGroupCollapsed(group_id, true));
    draw_canvas();
    REQUIRE(app.testSetGroupCollapsed(group_id, false));
    draw_canvas();

    const ImVec2 first_after = app.testGraphWidget().editorPositionForSave(first_node);
    const ImVec2 second_after = app.testGraphWidget().editorPositionForSave(second_node);
    CHECK(first_after.x == Catch::Approx(first_before.x));
    CHECK(first_after.y == Catch::Approx(first_before.y));
    CHECK(second_after.x == Catch::Approx(second_before.x));
    CHECK(second_after.y == Catch::Approx(second_before.y));
}

TEST_CASE_METHOD(AppFixture, "Agent moves to a collapsed member position that survives expansion",
                 "[agent_app][collapsed]") {
    app.newProject();
    const auto add = [&](double x, double y) {
        return app.testAgentCall(
            "circuit_edit",
            ordered_json{{"epoch", app.testProjectEpoch()},
                         {"ops", ordered_json::array({{{"op", "add"},
                                                       {"type", "generator"},
                                                       {"position", {{"x", x}, {"y", y}}}}})}});
    };
    REQUIRE_FALSE(add(20.0, 30.0).is_error);
    REQUIRE_FALSE(add(260.0, 140.0).is_error);

    const auto components = app.testComponents().all();
    REQUIRE(components.size() == 2);
    const int component_id = components[0]->id();
    const int first_node = components[0]->graphNodeId();
    const int second_node = components[1]->graphNodeId();
    const int group_id = app.testCreateGroup("Agent move test", {first_node, second_node});
    REQUIRE(group_id >= 0);
    REQUIRE(app.testSetGroupCollapsed(group_id, false));

    bool open = true;
    const auto draw_canvas = [&]() {
        runFrame([&]() { app.testGraphWidget().draw("Node Editor Test", &open); });
    };
    draw_canvas();
    REQUIRE(app.testSetGroupCollapsed(group_id, true));
    draw_canvas();

    const AgentToolResult before = app.testAgentCall("circuit_get", ordered_json::object());
    REQUIRE_FALSE(before.is_error);
    const auto &position = before.structured.at("components").at(0).at("position");
    const double target_x = position.at("x").get<double>() + 220.0;
    const double target_y = position.at("y").get<double>() + 45.0;
    const AgentToolResult move = app.testAgentCall(
        "circuit_edit",
        ordered_json{
            {"epoch", app.testProjectEpoch()},
            {"ops", ordered_json::array({{{"op", "move"},
                                          {"component", component_id},
                                          {"position", {{"x", target_x}, {"y", target_y}}}}})}});
    REQUIRE_FALSE(move.is_error);

    draw_canvas();
    const AgentToolResult still_collapsed =
        app.testAgentCall("circuit_get", ordered_json::object());
    REQUIRE_FALSE(still_collapsed.is_error);
    CHECK(still_collapsed.structured.at("components").at(0).at("position").at("x").get<double>() ==
          Catch::Approx(target_x));
    CHECK(still_collapsed.structured.at("components").at(0).at("position").at("y").get<double>() ==
          Catch::Approx(target_y));

    REQUIRE(app.testSetGroupCollapsed(group_id, false));
    draw_canvas();
    const AgentToolResult expanded = app.testAgentCall("circuit_get", ordered_json::object());
    REQUIRE_FALSE(expanded.is_error);
    CHECK(expanded.structured.at("components").at(0).at("position").at("x").get<double>() ==
          Catch::Approx(target_x));
    CHECK(expanded.structured.at("components").at(0).at("position").at("y").get<double>() ==
          Catch::Approx(target_y));
}

TEST_CASE_METHOD(AppFixture, "circuit_get positions include the current canvas pan",
                 "[agent_app]") {
    app.newProject();
    ImNodes::EditorContextSet(app.testGraphWidget().context());
    ImNodes::EditorContextResetPanning(ImVec2(0.0f, 0.0f));

    const AgentToolResult add = app.testAgentCall(
        "circuit_edit",
        ordered_json{{"epoch", app.testProjectEpoch()},
                     {"ops", ordered_json::array({{{"op", "add"},
                                                   {"type", "generator"},
                                                   {"position", {{"x", 20.0}, {"y", 30.0}}}}})}});
    REQUIRE_FALSE(add.is_error);

    bool open = true;
    const auto draw_canvas = [&]() {
        runFrame([&]() { app.testGraphWidget().draw("Node Editor Test", &open); });
    };
    draw_canvas();
    const AgentToolResult before = app.testAgentCall("circuit_get", ordered_json::object());
    REQUIRE_FALSE(before.is_error);
    const auto &before_position = before.structured.at("components").at(0).at("position");
    const double x_before = before_position.at("x").get<double>();
    const double y_before = before_position.at("y").get<double>();
    const std::uint64_t epoch = before.structured.at("epoch").get<std::uint64_t>();

    ImNodes::EditorContextSet(app.testGraphWidget().context());
    ImNodes::EditorContextResetPanning(ImVec2(120.0f, -40.0f));
    draw_canvas();

    const AgentToolResult after = app.testAgentCall("circuit_get", ordered_json::object());
    REQUIRE_FALSE(after.is_error);
    CHECK(after.structured.at("epoch") == epoch);
    CHECK(after.structured.at("components").at(0).at("position").at("x").get<double>() ==
          Catch::Approx(x_before + 120.0));
    CHECK(after.structured.at("components").at(0).at("position").at("y").get<double>() ==
          Catch::Approx(y_before - 40.0));
}

TEST_CASE_METHOD(AppFixture, "No-op agent move preserves checkpoint history", "[agent_app]") {
    app.newProject();
    const AgentToolResult add = app.testAgentCall(
        "circuit_edit",
        ordered_json{{"epoch", app.testProjectEpoch()},
                     {"ops", ordered_json::array({{{"op", "add"},
                                                   {"type", "generator"},
                                                   {"position", {{"x", 20.0}, {"y", 30.0}}}}})}});
    REQUIRE_FALSE(add.is_error);

    const AgentToolResult before = app.testAgentCall("circuit_get", ordered_json::object());
    REQUIRE_FALSE(before.is_error);
    const int component_id = before.structured.at("components").at(0).at("id").get<int>();
    const auto position = before.structured.at("components").at(0).at("position");
    const std::uint64_t revision = before.structured.at("revision").get<std::uint64_t>();
    const std::size_t checkpoint_count = app.testAgentHost().checkpoints().size();
    REQUIRE(checkpoint_count > 0);

    const AgentToolResult move = app.testAgentCall(
        "circuit_edit",
        ordered_json{
            {"epoch", app.testProjectEpoch()},
            {"ops", ordered_json::array(
                        {{{"op", "move"}, {"component", component_id}, {"position", position}}})}});
    REQUIRE_FALSE(move.is_error);
    CHECK(move.structured.at("applied").at(0).at("unchanged") == true);
    CHECK(app.testAgentHost().checkpoints().size() == checkpoint_count);

    const AgentToolResult after = app.testAgentCall("circuit_get", ordered_json::object());
    REQUIRE_FALSE(after.is_error);
    CHECK(after.structured.at("revision") == revision);
    CHECK(after.structured.at("dirty") == before.structured.at("dirty"));
}

TEST_CASE_METHOD(AppFixture, "Repeated move at fractional canvas pan is a no-op", "[agent_app]") {
    app.newProject();
    ImNodes::EditorContextSet(app.testGraphWidget().context());
    ImNodes::EditorContextResetPanning(ImVec2(-13.256257f, 0.5f));

    const AgentToolResult add = app.testAgentCall(
        "circuit_edit",
        ordered_json{{"epoch", app.testProjectEpoch()},
                     {"ops", ordered_json::array({{{"op", "add"},
                                                   {"type", "generator"},
                                                   {"position", {{"x", 0.0}, {"y", 0.0}}}}})}});
    REQUIRE_FALSE(add.is_error);
    const int component_id = add.structured.at("applied").at(0).at("component").get<int>();
    const std::uint64_t revision_after_add = add.structured.at("revision").get<std::uint64_t>();

    const ordered_json target{{"x", 56.11579513549805}, {"y", 12.0}};
    const auto move_to_target = [&]() {
        return app.testAgentCall(
            "circuit_edit", ordered_json{{"epoch", app.testProjectEpoch()},
                                         {"ops", ordered_json::array({{{"op", "move"},
                                                                       {"component", component_id},
                                                                       {"position", target}}})}});
    };

    const AgentToolResult first_move = move_to_target();
    REQUIRE_FALSE(first_move.is_error);
    CHECK(first_move.structured.at("revision") == revision_after_add + 1);
    const std::uint64_t revision_after_move =
        first_move.structured.at("revision").get<std::uint64_t>();
    const std::size_t checkpoint_count = app.testAgentHost().checkpoints().size();
    const AgentToolResult after_first_move =
        app.testAgentCall("circuit_get", ordered_json::object());
    REQUIRE_FALSE(after_first_move.is_error);
    CHECK(after_first_move.structured.at("components").at(0).at("position").at("x").get<double>() ==
          Catch::Approx(target.at("x").get<double>()).margin(1e-5));

    const AgentToolResult repeated_move = move_to_target();
    REQUIRE_FALSE(repeated_move.is_error);
    CHECK(repeated_move.structured.at("applied").at(0).at("unchanged") == true);
    CHECK(repeated_move.structured.at("revision") == revision_after_move);
    CHECK(app.testAgentHost().checkpoints().size() == checkpoint_count);
    const std::uint64_t revision_before_material_move =
        repeated_move.structured.at("revision").get<std::uint64_t>();
    const std::size_t checkpoints_before_material_move = app.testAgentHost().checkpoints().size();

    const double clearly_different_x = target.at("x").get<double>() + 0.01;
    const AgentToolResult material_move = app.testAgentCall(
        "circuit_edit",
        ordered_json{{"epoch", app.testProjectEpoch()},
                     {"ops", ordered_json::array(
                                 {{{"op", "move"},
                                   {"component", component_id},
                                   {"position", {{"x", clearly_different_x}, {"y", 12.0}}}}})}});
    REQUIRE_FALSE(material_move.is_error);
    CHECK_FALSE(material_move.structured.at("applied").at(0).contains("unchanged"));
    CHECK(material_move.structured.at("revision") == revision_before_material_move + 1);
    CHECK(app.testAgentHost().checkpoints().size() == checkpoints_before_material_move + 1);
}

TEST_CASE_METHOD(AppFixture, "circuit_get handles a library component before editor registration",
                 "[agent_app]") {
    app.newProject();

    // Create the window while the new project is empty. The next frame collapses an
    // already-existing window, so Begin() skips the node editor body after the library insert.
    bool open = true;
    runFrame([&]() { app.testGraphWidget().draw("Node Editor Test", &open); });

    const auto add_generator = [&](double x) {
        return app.testAgentCall(
            "circuit_edit",
            ordered_json{{"epoch", app.testProjectEpoch()},
                         {"ops", ordered_json::array({{{"op", "add"},
                                                       {"type", "generator"},
                                                       {"position", {{"x", x}, {"y", 30.0}}}}})}});
    };
    REQUIRE_FALSE(add_generator(20.0).is_error);
    REQUIRE_FALSE(add_generator(260.0).is_error);

    const auto previous_components = app.testComponents().all();
    REQUIRE(previous_components.size() == 2);
    const int previous_node_a = previous_components[0]->graphNodeId();
    const int previous_node_b = previous_components[1]->graphNodeId();
    const int highest_previous_node = std::max(previous_node_a, previous_node_b);
    REQUIRE(app.testRemoveComponent(previous_node_a));
    REQUIRE(app.testRemoveComponent(previous_node_b));

    IComponentEngine *component = app.testAddLibraryPart("AM1143");
    REQUIRE(component != nullptr);
    REQUIRE(component->graphNodeId() > highest_previous_node);
    CHECK(app.testGraphWidget().nodeGridPosition(component->graphNodeId()).x == -1.0f);
    CHECK(app.testGraphWidget().nodeGridPosition(component->graphNodeId()).y == -1.0f);

    runFrame([&]() {
        ImGui::SetNextWindowCollapsed(true, ImGuiCond_Always);
        app.testGraphWidget().draw("Node Editor Test", &open);
    });
    const ImGuiWindow *editor_window = ImGui::FindWindowByName("Node Editor Test");
    REQUIRE(editor_window != nullptr);
    REQUIRE(editor_window->Collapsed);

    const AgentToolResult result = app.testAgentCall("circuit_get", ordered_json::object());
    REQUIRE_FALSE(result.is_error);
    const auto &position = result.structured.at("components").at(0).at("position");
    CHECK(position.at("x").get<double>() == Catch::Approx(0.0));
    CHECK(position.at("y").get<double>() == Catch::Approx(0.0));

    const fs::path saved_path = makePath("unregistered_library_component.rfsim");
    app.saveProject(saved_path.string());
    std::ifstream saved_file(saved_path);
    REQUIRE(saved_file.good());
    const json saved_project = json::parse(saved_file);
    REQUIRE(saved_project.at("components").size() == 1);
    const auto &saved_position = saved_project.at("components").at(0).at("pos");
    CHECK(saved_position.at("x").get<double>() == Catch::Approx(0.0));
    CHECK(saved_position.at("y").get<double>() == Catch::Approx(0.0));
}
