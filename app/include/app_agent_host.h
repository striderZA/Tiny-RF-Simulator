#pragma once

#include "agent_host.h"
#include "node_graph_widget.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

// -----------------------------------------------------------------------
// AgentCheckpoint — a recorded snapshot of project state taken at the
// start of an agent tool call, kept for undo / revert.
// -----------------------------------------------------------------------
struct AgentCheckpoint {
    std::uint64_t id;
    std::chrono::system_clock::time_point time;
    std::string tool, client, summary;
    nlohmann::json snapshot;
};

// -----------------------------------------------------------------------
// AppAgentHost — app-side IAgentHost implementation.
//
// Owns checkpoints (newest 20), activity log (newest 50), node placement
// through the NodeGraphWidget, and modal detection through ImGui.
// -----------------------------------------------------------------------
class AppAgentHost final : public IAgentHost {
  public:
    struct Hooks {
        std::function<nlohmann::json()> capture;
        std::function<std::optional<std::string>()> project_name;
    };

    AppAgentHost(NodeGraphWidget &graph_widget, Hooks hooks);

    // -- Checkpoint query ------------------------------------------------
    const std::deque<AgentCheckpoint> &checkpoints() const { return m_checkpoints; }
    const std::deque<AgentActivity> &activity() const { return m_activity; }

    const AgentCheckpoint *findCheckpoint(std::uint64_t id) const;
    // Removes checkpoint |id| and all newer ones; returns their summaries.
    std::vector<std::string> removeCheckpointsFrom(std::uint64_t id);
    void clearCheckpoints();

    // -- IAgentHost ------------------------------------------------------
    void beginCheckpoint(const AgentCall &call) override;
    void commitCheckpoint(const std::string &summary) override;
    void discardCheckpoint() override;
    void placeComponents(const std::vector<AgentPlacement> &placements) override;
    bool appModalOpen() const override;
    std::optional<std::string> projectName() const override;
    void recordActivity(const AgentActivity &activity) override;

  private:
    NodeGraphWidget &m_graph_widget;
    Hooks m_hooks;

    std::deque<AgentCheckpoint> m_checkpoints;
    std::deque<AgentActivity> m_activity;

    // In-progress checkpoint (between begin/commit or begin/discard).
    std::string m_pending_tool, m_pending_client;
    nlohmann::json m_pending_snapshot;
    bool m_has_pending = false;

    std::uint64_t m_next_checkpoint_id = 1;
};

// Grid constants used by the auto-layout path of placeComponents.
inline constexpr float kAgentGridColumnWidth = 260.0f;
inline constexpr float kAgentGridRowHeight = 160.0f;