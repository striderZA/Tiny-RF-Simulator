#include "settings_panel_widget.h"

#include "imgui.h"
#include "ui_font_controller.h"

#include <cfloat>
#include <string>

namespace {

constexpr const char *kDefaultLabel = "Default (built-in)";
constexpr const char *kSampleLine = "The quick brown fox 0123456789 +-*/ dBm MHz";
// The amber tone the out-of-range numeric fields use, so a warning reads the same everywhere.
constexpr ImVec4 kNoteColor(1.0f, 0.75f, 0.35f, 1.0f);
constexpr int kListRows = 10;

} // namespace

void SettingsPanelWidget::draw(bool *p_open, UiFontController &fonts) {
    ImGui::SetNextWindowSize(ImVec2(460, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings", p_open)) {
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("UI font");
    ImGui::TextWrapped("Choose a monospaced font for the whole interface. It applies at once and "
                       "is saved with your settings. Default restores the built-in font.");
    if (!fonts.note().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kNoteColor);
        ImGui::TextWrapped("%s", fonts.note().c_str());
        ImGui::PopStyleColor();
    }

    const std::vector<std::string> &installed = fonts.installed();
    if (installed.empty())
        ImGui::TextDisabled("No monospaced fonts were found on this system, so only Default is "
                            "available.");

    const float list_height =
        ImGui::GetTextLineHeightWithSpacing() * kListRows + ImGui::GetStyle().FramePadding.y * 2.0f;
    const std::string &applied = fonts.appliedFamily();
    if (ImGui::BeginListBox("##ui_font_list", ImVec2(-FLT_MIN, list_height))) {
        if (ImGui::Selectable(kDefaultLabel, applied.empty()))
            fonts.select("");
        for (const std::string &family : installed) {
            const bool selected = family == applied;
            if (ImGui::Selectable(family.c_str(), selected))
                fonts.select(family);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndListBox();
    }

    // The preview is drawn in the font now in use. Choosing a family applies it at once, so the
    // name and the sample line show the chosen face.
    ImGui::SeparatorText("Preview");
    ImGui::TextUnformatted(applied.empty() ? kDefaultLabel : applied.c_str());
    ImGui::TextUnformatted(kSampleLine);

    ImGui::End();
}
