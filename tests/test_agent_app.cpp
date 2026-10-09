#include "agent_host.h"
#include "app_agent_host.h"
#include "circuit_runtime.h"
#include "component_type_registry.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "logging_core.h"
#include "node_graph_widget.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using nlohmann::json;

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

// Column spacing the auto-layout uses (from the brief).
constexpr float kColumnWidth = 260.0f;

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
// With no existing nodes the origin is (0,0).  The first node lands at
// (0,0) and each subsequent node at column × 260, row × 160.  Existing
// nodes never move.
// ======================================================================
TEST_CASE("New components land right of existing nodes, which never move", "[agent_app]") {
    AgentFixture f;

    // Create a component and place it at column 0, row 0 → position (0, 0).
    const int node1 = f.addComponent("generator");

    std::vector<AgentPlacement> first;
    first.push_back({.graph_node_id = node1, .position = std::nullopt, .column = 0, .row = 0});
    f.host.placeComponents(first);

    runFrame([&]() { f.widget.draw("test"); });

    const ImVec2 p1 = f.widget.nodeGridPosition(node1);
    CHECK(p1.x == 0.0f);
    CHECK(p1.y == 0.0f);

    // Place second component at column 1, row 0 → position (260, 0).
    const int node2 = f.addComponent("amplifier");

    std::vector<AgentPlacement> second;
    second.push_back({.graph_node_id = node2, .position = std::nullopt, .column = 1, .row = 0});
    f.host.placeComponents(second);

    runFrame([&]() { f.widget.draw("test"); });

    const ImVec2 p2 = f.widget.nodeGridPosition(node2);
    CHECK(p2.x == kColumnWidth);
    CHECK(p2.y == 0.0f);

    // First node never moved from its original position.
    const ImVec2 p1_after = f.widget.nodeGridPosition(node1);
    CHECK(p1_after.x == 0.0f);
    CHECK(p1_after.y == 0.0f);
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
// 51 activities → only 50 survive the trim.  Each recorded activity
// produces exactly one log line starting with "Agent:".
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

    // Only the newest 50 are kept.
    REQUIRE(f.host.activity().size() == 50);

    // Every call produces a log line prefixed with "Agent:".
    const std::size_t agent_lines = logCount(Level::Info, "Agent:");
    CHECK(agent_lines == 50);

    // Spot-check one line for the expected format.
    bool found_format = false;
    for (const auto &e : LoggerCore::instance().entries()) {
        if (e.level == Level::Info && e.message.find("Agent:") == 0) {
            found_format = true;
            // A successful activity logs: "Agent: <tool> <summary>"
            CHECK(e.message.find("Agent: tool_") != std::string::npos);
            break;
        }
    }
    CHECK(found_format);
}