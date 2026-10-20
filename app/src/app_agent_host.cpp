#include "app_agent_host.h"

#include "logging_core.h"

#include <imgui.h>
#include <imgui_internal.h> // GetTopMostPopupModal
#include <cstdlib>
#include <filesystem>
#include <unordered_set>

namespace {
constexpr float kAgentGridHorizontalGap = 32.0f;
}

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
    std::unordered_set<int> new_node_ids;
    for (const auto &placement : placements)
        new_node_ids.insert(placement.graph_node_id);

    const auto existing_bounds = m_graph_widget.nodeBoundsExcluding(new_node_ids);
    const ImVec2 origin = existing_bounds ? ImVec2(existing_bounds->max.x + kAgentGridHorizontalGap,
                                                   existing_bounds->min.y)
                                          : ImVec2(0.0f, 0.0f);

    for (const auto &p : placements) {
        ImVec2 pos;
        if (p.position.has_value()) {
            pos = ImVec2((*p.position)[0], (*p.position)[1]);
        } else {
            pos = ImVec2(origin.x + static_cast<float>(p.column) * kAgentGridColumnWidth,
                         origin.y + static_cast<float>(p.row) * kAgentGridRowHeight);
        }
        m_graph_widget.setNodeEditorSpacePosition(p.graph_node_id, pos);
    }
}
std::array<float, 2> AppAgentHost::nodeEditorSpacePosition(int graph_node_id) const {
    const ImVec2 position = m_graph_widget.editorPositionForSave(graph_node_id);
    return {position.x, position.y};
}

bool AppAgentHost::setNodeEditorSpacePosition(int graph_node_id, std::array<float, 2> position) {
    return m_graph_widget.setNodeEditorSpacePosition(graph_node_id,
                                                     ImVec2(position[0], position[1]));
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
// Project files
// ======================================================================
std::optional<std::string> AppAgentHost::projectPath() const {
    if (m_hooks.project_path)
        return m_hooks.project_path();
    return std::nullopt;
}

std::optional<std::string> AppAgentHost::componentLibraryRoot(std::string_view scope) const {
    namespace fs = std::filesystem;
    if (scope == "project") {
        std::error_code ec;
        const fs::path root = fs::current_path(ec);
        if (ec)
            return std::nullopt;
        return (root / "rf-sim-libraries").string();
    }
    if (scope != "global")
        return std::nullopt;

#ifdef _WIN32
    const char *home = std::getenv("USERPROFILE");
#else
    const char *home = std::getenv("HOME");
#endif
    if (!home || !*home)
        return std::nullopt;
    return (fs::path(home) / ".rf-sim" / "libraries").string();
}

bool AppAgentHost::saveProjectFile(const std::string &path) {
    return m_hooks.save_project && m_hooks.save_project(path);
}
std::vector<std::string> AppAgentHost::savedProjectDataFiles() const {
    if (m_hooks.saved_project_data_files)
        return m_hooks.saved_project_data_files();
    return {};
}

AgentProjectLoadResult AppAgentHost::loadProjectFile(const std::string &path) {
    if (!m_hooks.load_project)
        return {};
    return m_hooks.load_project(path);
}

// ======================================================================
// Activity log
// ======================================================================
void AppAgentHost::recordActivity(const AgentActivity &activity) {
    // Trim before pushing: if full, pop the oldest entry first.
    while (m_activity.size() >= 50)
        m_activity.pop_front();

    m_activity.push_back(activity);

    if (activity.ok) {
        LOG_INFO("Agent: %s %s", activity.tool.c_str(), activity.summary.c_str());
    } else {
        LOG_INFO("Agent: %s failed: %s", activity.tool.c_str(), activity.error_code.c_str());
    }
}