#include "app_agent_host.h"

#include "logging_core.h"

#include <imgui.h>
#include <imgui_internal.h> // GetTopMostPopupModal
#include <imnodes.h>

// ======================================================================
// Construction
// ======================================================================
AppAgentHost::AppAgentHost(NodeGraphWidget &graph_widget, Hooks hooks)
    : m_graph_widget(graph_widget), m_hooks(std::move(hooks)) {}

// ======================================================================
// Checkpoints
// ======================================================================
void AppAgentHost::beginCheckpoint(const AgentCall &call) {
    m_pending_tool = call.tool;
    m_pending_client = call.client;
    m_pending_snapshot = m_hooks.capture();
    m_has_pending = true;
}

void AppAgentHost::commitCheckpoint(const std::string &summary) {
    if (!m_has_pending) {
        LOG_WARN("AppAgentHost::commitCheckpoint called without a pending checkpoint");
        return;
    }

    AgentCheckpoint cp;
    cp.id = m_next_checkpoint_id++;
    cp.time = std::chrono::system_clock::now();
    cp.tool = std::move(m_pending_tool);
    cp.client = std::move(m_pending_client);
    cp.summary = summary;
    cp.snapshot = std::move(m_pending_snapshot);

    m_checkpoints.push_back(std::move(cp));
    m_has_pending = false;

    // Keep only the newest 20.
    while (m_checkpoints.size() > 20)
        m_checkpoints.pop_front();
}

void AppAgentHost::discardCheckpoint() {
    m_has_pending = false;
    m_pending_tool.clear();
    m_pending_client.clear();
    m_pending_snapshot = nlohmann::json();
}

const AgentCheckpoint *AppAgentHost::findCheckpoint(std::uint64_t id) const {
    for (const auto &cp : m_checkpoints) {
        if (cp.id == id)
            return &cp;
    }
    return nullptr;
}

std::vector<std::string> AppAgentHost::removeCheckpointsFrom(std::uint64_t id) {
    std::vector<std::string> removed;
    while (!m_checkpoints.empty()) {
        if (m_checkpoints.back().id >= id) {
            removed.push_back(m_checkpoints.back().summary);
            m_checkpoints.pop_back();
        } else {
            break;
        }
    }
    // removed was built from newest-first; reverse to chronological order.
    std::reverse(removed.begin(), removed.end());
    return removed;
}

void AppAgentHost::clearCheckpoints() { m_checkpoints.clear(); }

// ======================================================================
// Placement
// ======================================================================
void AppAgentHost::placeComponents(const std::vector<AgentPlacement> &placements) {
    ImNodes::EditorContextSet(m_graph_widget.context());

    for (const auto &p : placements) {
        ImVec2 pos;
        if (p.position.has_value()) {
            pos = ImVec2((*p.position)[0], (*p.position)[1]);
        } else {
            pos = ImVec2(static_cast<float>(p.column) * kAgentGridColumnWidth,
                         static_cast<float>(p.row) * kAgentGridRowHeight);
        }
        ImNodes::SetNodeEditorSpacePos(p.graph_node_id, pos);
    }
}

// ======================================================================
// Modal detection
// ======================================================================
bool AppAgentHost::appModalOpen() const {
    return ImGui::GetCurrentContext() && ImGui::GetTopMostPopupModal();
}

// ======================================================================
// Project name
// ======================================================================
std::optional<std::string> AppAgentHost::projectName() const {
    if (m_hooks.project_name)
        return m_hooks.project_name();
    return std::nullopt;
}

// ======================================================================
// Activity log
// ======================================================================
void AppAgentHost::recordActivity(const AgentActivity &activity) {
    // Trim before pushing: if full, pop the oldest entry first.
    bool slot_was_free = m_activity.size() < 50;
    while (m_activity.size() >= 50)
        m_activity.pop_front();

    m_activity.push_back(activity);

    // Log only when the new entry did not force an eviction, so exactly
    // the last 50 calls produce a log line.
    if (slot_was_free) {
        if (activity.ok) {
            LOG_INFO("Agent: %s %s", activity.tool.c_str(), activity.summary.c_str());
        } else {
            LOG_INFO("Agent: %s failed: %s", activity.tool.c_str(), activity.error_code.c_str());
        }
    }
}