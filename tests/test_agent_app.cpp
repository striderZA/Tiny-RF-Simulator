#include "agent_endpoint.h"
#include "agent_host.h"
#include "agent_panel_widget.h"
#include "agent_server.h"
#include "amplifier_engine.h"
#include "app.h"
#include "app_agent_host.h"
#include "circuit_runtime.h"
#include "component_type_registry.h"
#include "graph_editor_actions.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imnodes.h"
#include "implot.h"
#include "logging_core.h"
#include "node_graph_widget.h"
#include "signal_generator_engine.h"
#include "test_temp_paths.h"

#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using nlohmann::json;
using nlohmann::ordered_json;
namespace fs = std::filesystem;

namespace {

// -----------------------------------------------------------------------
// ImGuiFixture — bare ImGui/ImPlot/ImNodes context with font atlas built
// and a usable 1200×800 display.
// -----------------------------------------------------------------------
struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
        ImGui::GetIO().DisplaySize = ImVec2(1200.0f, 800.0f);
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char *pixels = nullptr;
        int w = 0, h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    }
    ~ImGuiFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

// -----------------------------------------------------------------------
// runFrame — one headless ImGui frame: NewFrame, callback, Render.
// -----------------------------------------------------------------------
void runFrame(const std::function<void()> &fn) {
    ImGui::NewFrame();
    fn();
    ImGui::Render();
}

// -----------------------------------------------------------------------
// logCount — number of log entries at |level| whose message contains
// |substr|.  Convenience for asserting log output.
// -----------------------------------------------------------------------
std::size_t logCount(Level level, const std::string &substr) {
    std::size_t n = 0;
    for (const auto &e : LoggerCore::instance().entries())
        if (e.level == level && e.message.find(substr) != std::string::npos)
            ++n;
    return n;
}

// -----------------------------------------------------------------------
// AgentFixture — shared fixture for app-level agent tests.
//
// Provides a live CircuitRuntime, a NodeGraphWidget that draws over it,
// and an AppAgentHost wired to both.  Log is cleared on construction so
// every test starts with a clean slate.
// -----------------------------------------------------------------------
struct AgentFixture : ImGuiFixture {
    CircuitRuntime runtime;
    NodeGraphWidget widget;
    AppAgentHost host;

    AgentFixture()
        : widget(runtime.graph(), NodeGraphWidgetActions{}),
          host(widget, {[]() -> json { return json::object(); },
                        []() -> std::optional<std::string> { return "test"; }}) {
        LoggerCore::instance().clear();
    }

    // addComponent — convenience: create a component through the runtime
    // and return its graph node id.
    int addComponent(std::string_view type) {
        const auto *desc = ComponentTypeRegistry::instance().find(type);
        REQUIRE(desc != nullptr);
        auto *comp = runtime.createComponent(desc->create);
        REQUIRE(comp != nullptr);
        return comp->graphNodeId();
    }
};

// -----------------------------------------------------------------------
// AppFixture — shared fixture for app-level RfSimulatorApp agent tests.
//
// Provides a temp directory per process for scratch files, a canned
// short-timeout AgentServerConfig, and a shared-fake-clock helper.
// -----------------------------------------------------------------------
struct AppFixture : ImGuiFixture {
    RfSimulatorApp app;
    fs::path temp_dir;

    // Fake clock for deterministic timing.
    struct FakeClock {
        std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
        void advance(std::chrono::milliseconds ms) { t += ms; }
    };
    std::shared_ptr<FakeClock> clock;

    AppFixture()
        : temp_dir(fs::temp_directory_path() / ("agent_app_" + test_temp_paths::processTag())) {
        fs::create_directories(temp_dir);
        std::string error;
        if (!ensurePrivateDirectory(temp_dir / "private", &error))
            throw std::runtime_error("Failed to ensure private agent endpoint directory: " + error);
        clock = std::make_shared<FakeClock>();
        LoggerCore::instance().clear();
    }

    ~AppFixture() {
        if (auto *server = app.testAgentServer())
            server->stop();
        std::error_code ec;
        fs::remove_all(temp_dir, ec);
    }

    fs::path makePath(const std::string &name) const { return temp_dir / name; }

    // Endpoint path for a test agent server.
    fs::path endpointPath() const { return makePath("private") / "agent_endpoint.json"; }

    // Server config with the fake clock and conservative timeouts.
    AgentServerConfig serverConfig() const {
        AgentServerConfig cfg;
        cfg.endpoint_file = endpointPath();
        cfg.app_version = "test";
        cfg.pump_budget = std::chrono::milliseconds(8);
        cfg.park_timeout = std::chrono::milliseconds(50);
        cfg.hello_timeout = std::chrono::milliseconds(100);
        return cfg;
    }

    // Server config with fake clock injected for deterministic timing.
    AgentServerConfig timedConfig() {
        auto c = serverConfig();
        auto shared = clock;
        c.now = [shared]() { return shared->t; };
        return c;
    }

    void writeText(const fs::path &path, const std::string &text) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    }
};

} // namespace

// ======================================================================
// Test: Checkpoint lifetime and removal
// ======================================================================
//
// 21 checkpoints → only 20 survive the trim.  removeCheckpointsFrom(id)
// removes that id and all newer ones, returning their summaries.
// ======================================================================
TEST_CASE("Checkpoints keep the newest 20 and removal returns the undone summaries",
          "[agent_app]") {
    AgentFixture f;

    // The checkpoint deque starts empty.
    REQUIRE(f.host.checkpoints().empty());

    // Create 21 checkpoints with sequential IDs 1..21.
    for (int i = 1; i <= 21; ++i) {
        AgentCall call;
        call.tool = "tool_" + std::to_string(i);
        call.client = "test";
        f.host.beginCheckpoint(call);
        f.host.commitCheckpoint("summary_" + std::to_string(i));
    }

    // Only the newest 20 are kept — ID 1 was dropped.
    REQUIRE(f.host.checkpoints().size() == 20);
    CHECK(f.host.findCheckpoint(1) == nullptr);
    CHECK(f.host.findCheckpoint(21) != nullptr);

    // removeCheckpointsFrom(15) removes IDs 15 through 21 (7 items)
    // and returns their summaries in chronological order.
    const std::vector<std::string> removed = f.host.removeCheckpointsFrom(15);
    REQUIRE(removed.size() == 7);
    CHECK(removed[0] == "summary_15");
    CHECK(removed[1] == "summary_16");
    CHECK(removed[2] == "summary_17");
    CHECK(removed[3] == "summary_18");
    CHECK(removed[4] == "summary_19");
    CHECK(removed[5] == "summary_20");
    CHECK(removed[6] == "summary_21");

    // Remaining: IDs 2 through 14 (13 items).
    REQUIRE(f.host.checkpoints().size() == 13);
    CHECK(f.host.findCheckpoint(14) != nullptr);
    CHECK(f.host.findCheckpoint(15) == nullptr);
}

// ======================================================================
// Test: Auto-layout placement
// ======================================================================
//
// The first automatic node on an empty canvas starts at the origin.  Later
// automatic placement must be based on existing canvas bounds, not on the
// new node's logical column alone, and must not move existing nodes.
// ======================================================================
TEST_CASE("New components land right of existing nodes, which never move", "[agent_app]") {
    AgentFixture f;

    const int node1 = f.addComponent("generator");
    std::vector<AgentPlacement> first;
    first.push_back({.graph_node_id = node1, .position = std::nullopt, .column = 0, .row = 0});
    f.host.placeComponents(first);

    runFrame([&]() { f.widget.draw("test"); });

    const ImVec2 origin = f.widget.nodeGridPosition(node1);
    CHECK(origin.x == 0.0f);
    CHECK(origin.y == 0.0f);

    const int existing_node = f.addComponent("amplifier");
    std::vector<AgentPlacement> existing_placement;
    existing_placement.push_back(
        {.graph_node_id = existing_node, .position = {{1000.0f, 0.0f}}, .column = 0, .row = 0});
    f.host.placeComponents(existing_placement);

    runFrame([&]() { f.widget.draw("test"); });

    const ImVec2 existing_before = f.widget.nodeGridPosition(existing_node);
    CHECK(existing_before.x == 1000.0f);
    CHECK(existing_before.y == 0.0f);
    const ImVec2 existing_dimensions = ImNodes::GetNodeDimensions(existing_node);

    const int new_node = f.addComponent("amplifier");
    std::vector<AgentPlacement> automatic_placement;
    automatic_placement.push_back(
        {.graph_node_id = new_node, .position = std::nullopt, .column = 0, .row = 0});
    f.host.placeComponents(automatic_placement);

    runFrame([&]() { f.widget.draw("test"); });

    const ImVec2 new_position = f.widget.nodeGridPosition(new_node);
    CHECK(new_position.x >= existing_before.x + existing_dimensions.x);
    const ImVec2 existing_after = f.widget.nodeGridPosition(existing_node);
    CHECK(existing_after.x == existing_before.x);
    CHECK(existing_after.y == existing_before.y);
    const ImVec2 origin_after = f.widget.nodeGridPosition(node1);
    CHECK(origin_after.x == origin.x);
    CHECK(origin_after.y == origin.y);
}

TEST_CASE("Automatic placement respects collapsed-group bounds and preserves members",
          "[agent_app][placement]") {
    AgentFixture f;
    GraphEditorActions graph_actions(f.runtime);

    const int member_a = f.addComponent("generator");
    const int member_b = f.addComponent("amplifier");
    std::vector<AgentPlacement> member_placements;
    member_placements.push_back(
        {.graph_node_id = member_a, .position = {{0.0f, 0.0f}}, .column = 0, .row = 0});
    member_placements.push_back(
        {.graph_node_id = member_b, .position = {{220.0f, 0.0f}}, .column = 0, .row = 0});
    f.host.placeComponents(member_placements);

    runFrame([&]() { f.widget.draw("test"); });

    const ImVec2 member_a_before = f.widget.nodeGridPosition(member_a);
    const ImVec2 member_b_before = f.widget.nodeGridPosition(member_b);
    REQUIRE(member_a_before.x == 0.0f);
    REQUIRE(member_a_before.y == 0.0f);
    REQUIRE(member_b_before.x == 220.0f);
    REQUIRE(member_b_before.y == 0.0f);
    ImNodes::EditorContextSet(f.widget.context());
    const ImVec2 member_a_dimensions = ImNodes::GetNodeDimensions(member_a);
    const ImVec2 member_b_dimensions = ImNodes::GetNodeDimensions(member_b);
    REQUIRE(member_a_dimensions.x > 0.0f);
    REQUIRE(member_b_dimensions.x > 0.0f);
    const float members_right = std::max(member_a_before.x + member_a_dimensions.x,
                                         member_b_before.x + member_b_dimensions.x);

    const std::string group_title =
        "Collapsed group title deliberately wider than its component members for placement";
    const int group_id = graph_actions.createGroup(group_title, {member_a, member_b});
    REQUIRE(graph_actions.setGroupCollapsed(group_id, true));

    runFrame([&]() { f.widget.draw("test"); });

    REQUIRE(f.widget.collapsedGroupBlockRendered(group_id));
    ImNodes::EditorContextSet(f.widget.context());
    const ImVec2 collapsed_position = ImNodes::GetNodeEditorSpacePos(group_id);
    const ImVec2 collapsed_dimensions = ImNodes::GetNodeDimensions(group_id);
    REQUIRE(collapsed_dimensions.x > 0.0f);
    REQUIRE(collapsed_position.x + collapsed_dimensions.x > members_right);

    const int new_node = f.addComponent("amplifier");
    std::vector<AgentPlacement> automatic_placement;
    automatic_placement.push_back(
        {.graph_node_id = new_node, .position = std::nullopt, .column = 0, .row = 0});
    f.host.placeComponents(automatic_placement);

    runFrame([&]() { f.widget.draw("test"); });

    REQUIRE(f.widget.collapsedGroupBlockRendered(group_id));
    ImNodes::EditorContextSet(f.widget.context());
    const ImVec2 block_position_after_placement = ImNodes::GetNodeEditorSpacePos(group_id);
    const ImVec2 block_dimensions_after_placement = ImNodes::GetNodeDimensions(group_id);
    const float block_right_after_placement =
        block_position_after_placement.x + block_dimensions_after_placement.x;
    const ImVec2 new_node_position = f.widget.nodeGridPosition(new_node);
    const ImVec2 member_a_after = f.widget.nodeGridPosition(member_a);
    const ImVec2 member_b_after = f.widget.nodeGridPosition(member_b);
    CHECK(member_a_after.x == member_a_before.x);
    CHECK(member_a_after.y == member_a_before.y);
    CHECK(member_b_after.x == member_b_before.x);
    CHECK(member_b_after.y == member_b_before.y);
    CHECK(new_node_position.x >= block_right_after_placement);
}

// ======================================================================
// Test: Explicit positions override auto-layout
// ======================================================================
//
// When an AgentPlacement carries a non-nullopt position, that position
// must be used instead of the column/row auto-layout.
// ======================================================================
TEST_CASE("Explicit positions win over the layout", "[agent_app]") {
    AgentFixture f;

    const int node1 = f.addComponent("generator");
    const int node2 = f.addComponent("amplifier");

    // Both at column 0, row 0 — the auto-layout would put both at (0, 0),
    // but node2 has an explicit position that must override it.
    std::vector<AgentPlacement> placements;
    placements.push_back({.graph_node_id = node1, .position = std::nullopt, .column = 0, .row = 0});
    placements.push_back(
        {.graph_node_id = node2, .position = {{999.0f, 555.0f}}, .column = 0, .row = 0});
    f.host.placeComponents(placements);

    runFrame([&]() { f.widget.draw("test"); });

    const ImVec2 p2 = f.widget.nodeGridPosition(node2);
    CHECK(p2.x == 999.0f);
    CHECK(p2.y == 555.0f);

    // If node1 was placed before node2, it should still show the column
    // position (0, 0) — the explicit position on node2 does not affect
    // node1.  (Node1 might also be at 0,0 if it was placed first, but
    // we only assert it is somewhere — the exact position depends on
    // the auto-layout implementation, which is tested above.)
    const ImVec2 p1 = f.widget.nodeGridPosition(node1);
    CHECK(p1.x >= 0.0f);
    CHECK(p1.y >= 0.0f);
}

// ======================================================================
// Test: Activity log and capacity
// ======================================================================
//
// 51 activities → only the newest 50 survive the trim.  Each call,
// including the evicting 51st, produces exactly one "Agent:" log line.
// ======================================================================
TEST_CASE("Activity keeps the newest 50 calls and logs one line per call", "[agent_app]") {
    AgentFixture f;

    // Record 51 activities.
    for (int i = 1; i <= 51; ++i) {
        AgentActivity act;
        act.time = std::chrono::system_clock::now();
        act.tool = "tool_" + std::to_string(i);
        act.client = "test";
        act.summary = "ran step " + std::to_string(i);
        act.ok = true;
        act.duration_ms = 1.0;
        f.host.recordActivity(act);
    }

    // The oldest call is evicted and the newest, evicting call is retained.
    REQUIRE(f.host.activity().size() == 50);
    CHECK(f.host.activity().front().tool == "tool_2");
    CHECK(f.host.activity().back().tool == "tool_51");

    // Logging records every call even when its panel record survives only by
    // displacing the oldest retained entry.
    const std::size_t agent_lines = logCount(Level::Info, "Agent:");
    CHECK(agent_lines == 51);
    const auto activity_log_entries = LoggerCore::instance().entries();
    for (int i = 1; i <= 51; ++i) {
        const std::string expected_prefix =
            "Agent: tool_" + std::to_string(i) + " ran step " + std::to_string(i);
        const auto matching_lines = std::count_if(
            activity_log_entries.begin(), activity_log_entries.end(),
            [&expected_prefix](const LogEntry &entry) {
                return entry.level == Level::Info && entry.message.find(expected_prefix) == 0;
            });
        CHECK(matching_lines == 1);
    }
}

// ======================================================================
// Test: The agent pump runs before the DSP update
//
// Submit a circuit_edit that adds a 1 GHz generator tone through the
// server queue, then call update_dsp once.  The pump() inside update_dsp
// must process the call before the DSP simulation runs, so the generator
// appears in the circuit and its tone is non-empty.
// ======================================================================
TEST_CASE_METHOD(AppFixture, "The agent pump runs before the DSP update", "[agent_app]") {
    app.newProject();
    std::string error;
    const bool started = app.testStartAgentServer(serverConfig(), &error);
    CAPTURE(error);
    CAPTURE(app.testAgentServer()->status().error);
    REQUIRE(started);

    // Enqueue a circuit_edit that adds a generator with a 1 GHz tone.
    AgentCall call;
    call.tool = "circuit_edit";
    call.arguments = ordered_json{
        {"epoch", app.testProjectEpoch()},
        {"ops",
         ordered_json::array(
             {{{"op", "add"},
               {"ref", "gen"},
               {"type", "generator"},
               {"params",
                {{"tones", {{{"freq_Hz", 1.0e9}, {"power_dBm", -10.0}, {"phase_deg", 0.0}}}}}}}})}};
    call.client = "test";

    bool completed = false;
    AgentToolResult result;
    app.testAgentServer()->submit(call, [&](const AgentToolResult &r) {
        completed = true;
        result = r;
    });

    // No pump yet — generator does not exist.
    CHECK(app.testComponents().byType<SignalGeneratorEngine>().empty());

    // update_dsp starts with pump() → processes the queued call → DSP runs.
    app.update_dsp();

    REQUIRE(completed);
    CHECK_FALSE(result.is_error);

    const auto gens = app.testComponents().byType<SignalGeneratorEngine>();
    REQUIRE(gens.size() == 1);
    CHECK(gens[0]->toneCount() == 1);
    CHECK(gens[0]->tones()[0].freq_Hz == Catch::Approx(1.0e9));
    REQUIRE_FALSE(gens[0]->node().outputs.empty());
    CHECK(gens[0]->node().outputs[0].tones.size() == 1);
}

// ======================================================================
// Test: Agent calls park while an app modal is open
//
// A server-submitted call must not execute while ImGui reports a modal is
// open.  The pump parks, and only after the park_timeout elapses does the
// server complete the call with BUSY.
// ======================================================================
TEST_CASE_METHOD(AppFixture, "Agent calls park while an app modal is open", "[agent_app]") {
    auto cfg = timedConfig();
    cfg.park_timeout = std::chrono::milliseconds(50);
    std::string error;
    const bool started = app.testStartAgentServer(cfg, &error);
    CAPTURE(error);
    CAPTURE(app.testAgentServer()->status().error);
    REQUIRE(started);

    // Enqueue a circuit_edit call.
    AgentCall edit;
    edit.tool = "circuit_edit";
    edit.arguments = ordered_json{
        {"epoch", app.testProjectEpoch()},
        {"ops", ordered_json::array({{{"op", "add"}, {"ref", "gen"}, {"type", "generator"}}})}};
    edit.client = "test";

    bool completed = false;
    AgentToolResult result;
    app.testAgentServer()->submit(edit, [&](const AgentToolResult &r) {
        completed = true;
        result = r;
    });

    // First pump with a modal open → call is parked.
    runFrame([&]() {
        ImGui::OpenPopup("test_modal");
        REQUIRE(ImGui::BeginPopupModal("test_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize));
        ImGui::EndPopup();
        app.testAgentServer()->pump();
    });
    CHECK_FALSE(completed);

    // Advance fake clock past park timeout.
    clock->advance(std::chrono::milliseconds(60));

    // Second pump → parked call times out → BUSY.
    runFrame([&]() {
        ImGui::OpenPopup("test_modal");
        if (ImGui::BeginPopupModal("test_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            ImGui::EndPopup();
        app.testAgentServer()->pump();
    });
    REQUIRE(completed);
    CHECK(result.is_error);

    // The error code is BUSY.  (The structured field is "error"/"code".)
    REQUIRE(result.structured.contains("error"));
    REQUIRE(result.structured["error"].contains("code"));
    CHECK(result.structured["error"]["code"] == "BUSY");
}

// ======================================================================
// Test: Revert restores the checkpoint and tells the agent why
//
// Two circuit_edit calls produce checkpoints.  Reverting the second one
// restores the snapshot from before it was applied, advances the epoch
// by one, trims the reverting checkpoint, marks dirty, and arranges for
// the next stale-epoch result to report cause "reverted" with the undone
// summary.
// ======================================================================
TEST_CASE_METHOD(AppFixture, "Revert restores the checkpoint and tells the agent why",
                 "[agent_app]") {
    app.newProject();
    REQUIRE(app.testProjectEpoch() > 0);

    // First edit: add a generator.
    const auto r1 = app.testAgentCall(
        "circuit_edit",
        ordered_json{{"epoch", app.testProjectEpoch()},
                     {"ops", ordered_json::array(
                                 {{{"op", "add"}, {"ref", "gen"}, {"type", "generator"}}})}});
    CHECK_FALSE(r1.is_error);
    const auto epoch_after_first = app.testProjectEpoch();

    // Second edit: add an amplifier.
    const auto r2 = app.testAgentCall(
        "circuit_edit",
        ordered_json{{"epoch", app.testProjectEpoch()},
                     {"ops", ordered_json::array(
                                 {{{"op", "add"}, {"ref", "amp"}, {"type", "amplifier"}}})}});
    CHECK_FALSE(r2.is_error);
    const auto epoch_after_second = app.testProjectEpoch();
    // Circuit edits advance projectRevision(), never the runtime epoch.
    CHECK(epoch_after_second == epoch_after_first);

    // The agent host has two checkpoints.  The frontier one (id 2) was
    // taken before the second edit was applied, so its snapshot reflects
    // the state *after* the first edit.
    auto &checkpoints = app.testAgentHost().checkpoints();
    REQUIRE(checkpoints.size() == 2);

    // The frontier checkpoint (id 2) was taken before the second edit was
    // applied, so its snapshot reflects the state *after* the first edit.
    const std::uint64_t revert_id = checkpoints[1].id;
    const json checkpoint_snapshot = checkpoints[1].snapshot;

    // Revert the second checkpoint.
    app.revertAgentCheckpoint(revert_id);

    // Epoch advanced by exactly one.
    CHECK(app.testProjectEpoch() == epoch_after_second + 1);

    // Only one checkpoint remains (the first edit's).
    CHECK(app.testAgentHost().checkpoints().size() == 1);

    // toJson now matches the pre-edit-2 snapshot (the restored state).
    const json state_now = app.testProjectSerializer().toJson({std::nullopt, false});
    CHECK(state_now == checkpoint_snapshot);

    // Next call with a stale epoch returns STALE_EPOCH with cause
    // "reverted" and the undone summary.
    const auto stale = app.testAgentCall(
        "circuit_edit",
        ordered_json{{"epoch", app.testProjectEpoch() - 1},
                     {"ops", ordered_json::array(
                                 {{{"op", "add"}, {"ref", "stale"}, {"type", "generator"}}})}});
    REQUIRE(stale.is_error);
    CHECK(stale.structured.at("error").at("code") == "STALE_EPOCH");
    const auto &details = stale.structured.at("error").at("details");
    CHECK(details.at("cause") == "reverted");
    REQUIRE(details.contains("undone"));
    REQUIRE(details["undone"].is_array());
    CHECK(details["undone"].size() == 1);

    // Project is dirty after revert.
    CHECK(app.isDirty());
}

// ======================================================================
// Test: Revert keeps S-parameter data from outside the project folder
//
// Add the AM1143 library amplifier via an agent circuit_edit, which loads
// its S-parameter data from the component_data library (outside any
// project folder).  The checkpoint captures the verbatim external path.
// After reverting the second of two edits, the amplifier must still
// report sparamMode() as true.
// ======================================================================
TEST_CASE_METHOD(AppFixture,
                 "Revert keeps S-parameter data from outside the project "
                 "folder",
                 "[agent_app]") {
    // Untitled project (no current path) — all S-param paths are external.
    app.newProject();

    // First edit: add the AM1143 library amplifier.
    const auto r1 = app.testAgentCall(
        "circuit_edit",
        ordered_json{
            {"epoch", app.testProjectEpoch()},
            {"ops", ordered_json::array({{{"op", "add"},
                                          {"ref", "amp"},
                                          {"library_part", {{"part_number", "AM1143"}}}}})}});
    CHECK_FALSE(r1.is_error);

    auto amps = app.testComponents().byType<AmplifierEngine>();
    REQUIRE(amps.size() == 1);
    auto *amp = amps[0];

    // The library part auto-loads S-param data from its data_files path
    // (which lives under component_data/library/ — outside any project).
    CHECK(amp->sparamMode());

    // Make a second edit to create a checkpoint we can revert.
    const auto r2 = app.testAgentCall(
        "circuit_edit",
        ordered_json{{"epoch", app.testProjectEpoch()},
                     {"ops", ordered_json::array(
                                 {{{"op", "add"}, {"ref", "gen"}, {"type", "generator"}}})}});
    CHECK_FALSE(r2.is_error);

    // Revert the second checkpoint (the generator add).
    auto &checkpoints = app.testAgentHost().checkpoints();
    REQUIRE(checkpoints.size() == 2);
    app.revertAgentCheckpoint(checkpoints[1].id);

    // The amplifier's S-param mode is still on after revert.
    const auto restored = app.testComponents().byType<AmplifierEngine>();
    REQUIRE(restored.size() == 1);
    CHECK(restored[0]->sparamMode());

    // No generator remains (the revert removed it).
    CHECK(app.testComponents().byType<SignalGeneratorEngine>().empty());
}

// ======================================================================
// Test: Project replacements advance the epoch and clear checkpoints
//
// Every full replacement (New, resetting load, tutorial start, revert)
// advances the project epoch by exactly one and clears runtime
// checkpoints.  A failed intact load (malformed optional scalar inside
// otherwise-valid top-level structure) leaves the epoch unchanged.
// ======================================================================
TEST_CASE_METHOD(AppFixture,
                 "Project replacements advance the epoch and clear "
                 "checkpoints",
                 "[agent_app]") {
    app.newProject();
    const auto initial_epoch = app.testProjectEpoch();

    // Create some agent checkpoints via a circuit edit.
    app.testAgentCall(
        "circuit_edit",
        ordered_json{{"epoch", app.testProjectEpoch()},
                     {"ops", ordered_json::array(
                                 {{{"op", "add"}, {"ref", "gen"}, {"type", "generator"}}})}});
    REQUIRE(app.testAgentHost().checkpoints().size() == 1);

    // --- New ---
    app.newProject();
    CHECK(app.testProjectEpoch() == initial_epoch + 1);
    CHECK(app.testAgentHost().checkpoints().empty());

    // Re-establish checkpoints for subsequent tests.
    app.testAgentCall(
        "circuit_edit",
        ordered_json{{"epoch", app.testProjectEpoch()},
                     {"ops", ordered_json::array(
                                 {{{"op", "add"}, {"ref", "gen"}, {"type", "generator"}}})}});
    REQUIRE(app.testAgentHost().checkpoints().size() == 1);

    // --- Resetting load (section-shape failure clears the previous path) ---
    const auto load_reset_path = makePath("load_reset.rfsim");
    writeText(load_reset_path, R"({"components": 5, "window_state": {}, "graph_state": {}})");
    const auto epoch_before_load = app.testProjectEpoch();
    app.loadProject(load_reset_path.string());

    // A section-shape failure resets the project → epoch advances.
    CHECK(app.testProjectEpoch() == epoch_before_load + 1);
    CHECK(app.testAgentHost().checkpoints().empty());

    // Re-establish checkpoints and a valid baseline.
    const auto valid_path = makePath("valid.rfsim");
    app.testAgentCall(
        "circuit_edit",
        ordered_json{{"epoch", app.testProjectEpoch()},
                     {"ops", ordered_json::array(
                                 {{{"op", "add"}, {"ref", "gen"}, {"type", "generator"}}})}});
    REQUIRE(app.testAgentHost().checkpoints().size() == 1);
    app.saveProject(valid_path.string());

    // Reload the valid project (a full replacement).
    const auto epoch_before_reload = app.testProjectEpoch();
    app.testAgentCall(
        "circuit_edit",
        ordered_json{{"epoch", app.testProjectEpoch()},
                     {"ops", ordered_json::array(
                                 {{{"op", "add"}, {"ref", "amp"}, {"type", "amplifier"}}})}});
    app.loadProject(valid_path.string());

    // A valid load advances epoch by exactly one (fromJson → one reset)
    // and clears runtime checkpoints.  The intervening testAgentCall
    // advances the revision, never the epoch.
    CHECK(app.testProjectEpoch() == epoch_before_reload + 1);
    CHECK(app.testAgentHost().checkpoints().empty());

    // --- Tutorial (via planned testStartTutorial accessor) ---
    // startTutorial() runs newProject() (+1 epoch), seeds the tutorial
    // sandbox, then notes Tutorial as the cause.  The total epoch advance
    // is +1 (the reset from newProject).
    //
    // NOTE: RfSimulatorApp::startTutorial() is private; the production
    // task must expose a testStartTutorial() accessor (mirroring the
    // testStartAgentServer pattern) for this sub-case.
    app.testAgentCall(
        "circuit_edit",
        ordered_json{{"epoch", app.testProjectEpoch()},
                     {"ops", ordered_json::array(
                                 {{{"op", "add"}, {"ref", "gen"}, {"type", "generator"}}})}});
    REQUIRE(app.testAgentHost().checkpoints().size() == 1);
    const auto epoch_before_tutorial = app.testProjectEpoch();
    app.testStartTutorial();
    CHECK(app.testProjectEpoch() == epoch_before_tutorial + 1);
    CHECK(app.testAgentHost().checkpoints().empty());

    // --- Failed intact load (optional scalar wrong type) ---
    // Top-level shapes are valid (components array, window_state object),
    // but an optional scalar inside window_state is a string instead of
    // bool, which the serializer rejects without resetting the project.
    const auto malformed_path = makePath("malformed.rfsim");
    writeText(
        malformed_path,
        R"({"components": [], "window_state": {"spectrum_analyzer": "yes"}, "graph_state": {}})");

    // Establish a baseline with checkpoints in the live project.
    app.testAgentCall(
        "circuit_edit",
        ordered_json{{"epoch", app.testProjectEpoch()},
                     {"ops", ordered_json::array(
                                 {{{"op", "add"}, {"ref", "gen"}, {"type", "generator"}}})}});
    const auto epoch_before_malformed = app.testProjectEpoch();
    const auto n_checkpoints = app.testAgentHost().checkpoints().size();

    app.loadProject(malformed_path.string());

    // Epoch unchanged, checkpoints untouched.
    CHECK(app.testProjectEpoch() == epoch_before_malformed);
    CHECK(app.testAgentHost().checkpoints().size() == n_checkpoints);
}

// ======================================================================
// Test: Stopping the server removes the endpoint file
//
// Starting the agent server creates the endpoint file on disk.  Stopping
// the server (via setAgentServerEnabled(false)) removes it.
// ======================================================================
TEST_CASE_METHOD(AppFixture, "Stopping the server removes the endpoint file", "[agent_app]") {
    std::string error;
    const bool started = app.testStartAgentServer(serverConfig(), &error);
    CAPTURE(error);
    CAPTURE(app.testAgentServer()->status().error);
    REQUIRE(started);

    // The endpoint file exists after start.
    CHECK(app.agentServerRunning());
    CHECK(fs::exists(endpointPath()));

    // Stop the server via the public enable toggle.
    app.setAgentServerEnabled(false);

    // The endpoint file is removed and the server is no longer running.
    CHECK_FALSE(fs::exists(endpointPath()));
    CHECK_FALSE(app.agentServerRunning());
}

// ======================================================================
// Test: Agent panel renders every server state and populated history
// ======================================================================
TEST_CASE("The Agent panel renders every server state", "[agent_app]") {
    ImGuiFixture fixture;
    AgentPanelWidget widget;

    const auto now = std::chrono::system_clock::now();
    std::deque<AgentActivity> activity;
    for (int i = 0; i < 3; ++i) {
        AgentActivity entry;
        entry.time = now - std::chrono::seconds(3 - i);
        entry.tool = i == 0 ? "circuit_get" : "circuit_edit";
        entry.client = "ui-test-client";
        entry.summary = "activity " + std::to_string(i + 1);
        entry.ok = i != 1;
        entry.error_code = i == 1 ? "INVALID_ARGUMENT" : "";
        entry.duration_ms = 1.0 + i;
        activity.push_back(std::move(entry));
    }

    std::deque<AgentCheckpoint> checkpoints;
    for (std::uint64_t id : {41u, 42u}) {
        AgentCheckpoint checkpoint;
        checkpoint.id = id;
        checkpoint.time = now;
        checkpoint.tool = "circuit_edit";
        checkpoint.client = "ui-test-client";
        checkpoint.summary = "checkpoint " + std::to_string(id);
        checkpoint.snapshot = json::object();
        checkpoints.push_back(std::move(checkpoint));
    }

    const auto makeStatus = [](AgentServerState state, std::string client_name = {},
                               std::string client_version = {}, std::string error = {}) {
        AgentServerStatus status;
        status.state = state;
        status.client_name = std::move(client_name);
        status.client_version = std::move(client_version);
        status.error = std::move(error);
        return status;
    };
    struct StateInput {
        AgentServerStatus status;
        bool server_on;
    };
    const std::array<StateInput, 5> states{{
        {makeStatus(AgentServerState::Off), false},
        {makeStatus(AgentServerState::Waiting), true},
        {makeStatus(AgentServerState::Connected, "ui-test-client", "1.2"), true},
        {makeStatus(AgentServerState::Busy, "ui-test-client", "1.2"), true},
        {makeStatus(AgentServerState::Error, {}, {}, "test listener failure"), true},
    }};

    std::array<bool, 5> exercised_states{};
    const std::string bridge_path = "C:\\RF Simulator\\rf-sim-mcp.exe";
    for (const StateInput &state : states) {
        AgentPanelView view;
        view.server_on = state.server_on;
        view.status = state.status;
        view.bridge_path = bridge_path;
        view.activity = &activity;
        view.checkpoints = &checkpoints;

        const auto state_index = static_cast<std::size_t>(view.status.state);
        REQUIRE(state_index < exercised_states.size());
        exercised_states[state_index] = true;

        CHECK(view.server_on == state.server_on);
        switch (view.status.state) {
        case AgentServerState::Off:
            CHECK_FALSE(view.server_on);
            break;
        case AgentServerState::Waiting:
            CHECK(view.server_on);
            break;
        case AgentServerState::Connected:
            CHECK(view.server_on);
            CHECK(view.status.client_name == "ui-test-client");
            CHECK(view.status.client_version == "1.2");
            break;
        case AgentServerState::Busy:
            CHECK(view.server_on);
            CHECK(view.status.client_name == "ui-test-client");
            CHECK(view.status.client_version == "1.2");
            break;
        case AgentServerState::Error:
            CHECK(view.server_on);
            CHECK(view.status.error == "test listener failure");
            break;
        }
        CHECK_FALSE(view.bridge_path.empty());
        REQUIRE(view.activity != nullptr);
        REQUIRE(view.checkpoints != nullptr);
        REQUIRE(view.activity->size() == 3);
        REQUIRE(view.checkpoints->size() == 2);
        for (const AgentActivity &entry : *view.activity) {
            CHECK_FALSE(entry.tool.empty());
            CHECK_FALSE(entry.client.empty());
            CHECK_FALSE(entry.summary.empty());
        }
        for (const AgentCheckpoint &checkpoint : *view.checkpoints) {
            CHECK(checkpoint.id != 0);
            CHECK_FALSE(checkpoint.tool.empty());
            CHECK_FALSE(checkpoint.client.empty());
            CHECK(checkpoint.snapshot.is_object());
            CHECK_FALSE(checkpoint.summary.empty());
        }

        bool open = true;
        runFrame([&]() { widget.draw("Agent", &open, view); });
        CHECK(open);
        CHECK(ImGui::FindWindowByName("Agent") != nullptr);
    }
    CHECK(exercised_states[static_cast<std::size_t>(AgentServerState::Off)]);
    CHECK(exercised_states[static_cast<std::size_t>(AgentServerState::Waiting)]);
    CHECK(exercised_states[static_cast<std::size_t>(AgentServerState::Connected)]);
    CHECK(exercised_states[static_cast<std::size_t>(AgentServerState::Busy)]);
    CHECK(exercised_states[static_cast<std::size_t>(AgentServerState::Error)]);
}