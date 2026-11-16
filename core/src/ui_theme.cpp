#include "ui_theme.h"

namespace {

constexpr ImU32 kDangerFill = IM_COL32(0x3A, 0x1A, 0x1A, 0xFF);
constexpr ImU32 kDangerHovered = IM_COL32(0x4A, 0x20, 0x20, 0xFF);
constexpr ImU32 kDangerActive = IM_COL32(0x5A, 0x26, 0x26, 0xFF);
constexpr ImU32 kDangerText = IM_COL32(0xFF, 0x8A, 0x80, 0xFF);

void pushDangerColors() {
    ImGui::PushStyleColor(ImGuiCol_Button, kDangerFill);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kDangerHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kDangerActive);
    ImGui::PushStyleColor(ImGuiCol_Text, kDangerText);
}

} // namespace

namespace ui {

bool dangerButton(const char *label, const ImVec2 &size) {
    pushDangerColors();
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

bool dangerSmallButton(const char *label) {
    pushDangerColors();
    const bool pressed = ImGui::SmallButton(label);
    ImGui::PopStyleColor(4);
    return pressed;
}

} // namespace ui
