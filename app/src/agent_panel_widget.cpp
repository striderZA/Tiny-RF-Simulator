#include "agent_panel_widget.h"

#include "imgui.h"

#include <string>

namespace {
std::string statusText(const AgentServerStatus &status) {
    switch (status.state) {
    case AgentServerState::Off:
        return "Off";
    case AgentServerState::Waiting:
        return "Waiting for a client";
    case AgentServerState::Connected:
        return "Connected: " + status.client_name + " " + status.client_version;
    case AgentServerState::Busy:
        return "Busy: calls are waiting";
    case AgentServerState::Error:
        return "Error: " + (status.error.empty() ? std::string("unknown error") : status.error);
    }
    return "Off";
}

constexpr const char *kRevertPopupTitle = "Revert Agent Changes";
constexpr const char *kRevertPopupText =
    "This restores the project to how it was before this call and discards every later change, "
    "including your own. Continue?";
} // namespace

void AgentPanelWidget::draw(const char *title, bool *open, const AgentPanelView &view) {
    bool open_revert_popup = false;
    const bool panel_visible = ImGui::Begin(title, open);
    if (panel_visible) {
        bool server_on = view.server_on;
        if (ImGui::Checkbox("Agent Server", &server_on) && onServerToggled)
            onServerToggled(server_on);

        const std::string status = statusText(view.status);
        ImGui::Text("Status: %s", status.c_str());
        ImGui::TextWrapped("Bridge: %s", view.bridge_path.c_str());
        ImGui::Separator();

        ImGui::TextUnformatted("Recent calls");
        if (!view.activity || view.activity->empty()) {
            ImGui::TextDisabled("No recent calls.");
        } else if (ImGui::BeginChild("##agent_recent_calls", ImVec2(0.0f, 160.0f), true)) {
            int index = 0;
            for (auto entry = view.activity->rbegin(); entry != view.activity->rend(); ++entry) {
                ImGui::PushID(index++);
                const char *result = entry->ok ? "Success" : "Error";
                if (!entry->ok && !entry->error_code.empty())
                    ImGui::Text("%s | %s (%s) | %.1f ms", entry->tool.c_str(), result,
                                entry->error_code.c_str(), entry->duration_ms);
                else
                    ImGui::Text("%s | %s | %.1f ms", entry->tool.c_str(), result,
                                entry->duration_ms);
                ImGui::TextWrapped("%s", entry->summary.c_str());
                ImGui::Separator();
                ImGui::PopID();
            }
        }
        if (view.activity && !view.activity->empty())
            ImGui::EndChild();

        ImGui::TextUnformatted("Checkpoints");
        if (!view.checkpoints || view.checkpoints->empty()) {
            ImGui::TextDisabled("No checkpoints.");
        } else if (ImGui::BeginChild("##agent_checkpoints", ImVec2(0.0f, 150.0f), true)) {
            for (auto checkpoint = view.checkpoints->rbegin();
                 checkpoint != view.checkpoints->rend(); ++checkpoint) {
                ImGui::PushID(static_cast<int>(checkpoint->id));
                ImGui::Text("%s", checkpoint->tool.c_str());
                ImGui::TextWrapped("%s", checkpoint->summary.c_str());
                if (ImGui::Button("Revert to before this call")) {
                    m_pending_checkpoint_id = checkpoint->id;
                    open_revert_popup = true;
                }
                ImGui::Separator();
                ImGui::PopID();
            }
        }
        if (view.checkpoints && !view.checkpoints->empty())
            ImGui::EndChild();
    }
    if (open_revert_popup)
        ImGui::OpenPopup(kRevertPopupTitle);

    if (m_pending_checkpoint_id) {
        bool modal_open = true;
        if (ImGui::BeginPopupModal(kRevertPopupTitle, &modal_open,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("%s", kRevertPopupText);
            if (ImGui::Button("Revert", ImVec2(120.0f, 0.0f))) {
                const std::uint64_t checkpoint_id = *m_pending_checkpoint_id;
                m_pending_checkpoint_id.reset();
                ImGui::CloseCurrentPopup();
                if (onRevert)
                    onRevert(checkpoint_id);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
                m_pending_checkpoint_id.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (!modal_open)
            m_pending_checkpoint_id.reset();
    }

    ImGui::End();
}
