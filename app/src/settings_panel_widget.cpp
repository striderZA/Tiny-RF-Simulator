#include "settings_panel_widget.h"

#include "imgui.h"
#include "ui_font_controller.h"

#include <cfloat>
#include <string>

namespace {

constexpr const char *kDefaultLabel = "Default (built-in)";
constexpr const char *kSampleLine = "The quick brown fox 0123456789 +-*/ dBm MHz";
constexpr const char *kBoldSampleLine = "Bold text: 0123456789 dBm MHz";
// The amber tone the out-of-range numeric fields use, so a warning reads the same everywhere.
constexpr ImVec4 kNoteColor(1.0f, 0.75f, 0.35f, 1.0f);
constexpr int kListRows = 10;

// Section headings are drawn in the emphasis slot, so bold text is in use wherever a heading is.
void heading(const UiFontController &fonts, const char *text) {
    ImGui::PushFont(fonts.emphasisFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::SeparatorText(text);
    ImGui::PopFont();
}

} // namespace

void SettingsPanelWidget::draw(bool *p_open, UiFontController &fonts) {
    // Fits the largest UI font (24 px) without a scrollbar: the controls and preview are below the
    // fold otherwise.
    ImGui::SetNextWindowSize(ImVec2(540, 800), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings", p_open)) {
        ImGui::End();
        return;
    }

    heading(fonts, "UI font");
    ImGui::TextWrapped("Choose a monospaced font for the whole interface. It applies at once and "
                       "is saved with your settings. Default restores the built-in font, size, and "
                       "style.");
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

    // Style and emphasis list only the faces the applied family has.
    if (!applied.empty()) {
        heading(fonts, "Style");
        const std::vector<ui_font::FontFace> &faces = fonts.appliedFaces();
        if (ImGui::BeginCombo("Style", fonts.appliedBodyFace().style.c_str())) {
            for (const ui_font::FontFace &face : faces) {
                const bool selected = face.style == fonts.appliedBodyFace().style;
                if (ImGui::Selectable(face.style.c_str(), selected))
                    fonts.selectStyle(face);
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::TextUnformatted("Bold text uses");
        ImGui::SameLine();
        // Medium shows only where the family has it. A saved Medium that the family lacks falls
        // back to Bold, the face bold text actually uses, so the Bold radio is the one selected.
        const bool medium_chosen = fonts.savedEmphasis() == ui_font::Emphasis::Medium &&
                                   ui_font::offersEmphasis(faces, ui_font::Emphasis::Medium);
        const bool bold_chosen = !medium_chosen;
        if (ImGui::RadioButton("Bold", bold_chosen))
            fonts.selectEmphasis(ui_font::Emphasis::Bold);
        if (ui_font::offersEmphasis(faces, ui_font::Emphasis::Medium)) {
            ImGui::SameLine();
            if (ImGui::RadioButton("Medium", medium_chosen))
                fonts.selectEmphasis(ui_font::Emphasis::Medium);
        }
    }

    heading(fonts, "Size");
    // The slider shows the requested size. Each frame it reports whether it is held, so the atlas
    // rebuilds once on release, or once the value settles while held.
    int size_px = static_cast<int>(fonts.requestedSize() + 0.5f);
    ImGui::SliderInt("Font size", &size_px, ui_font::kMinSizePx, ui_font::kMaxSizePx, "%d px");
    fonts.setSize(size_px, ImGui::IsItemActive());

    // The preview is drawn in the faces now in use, so a chosen face shows at once.
    heading(fonts, "Preview");
    if (applied.empty()) {
        ImGui::TextUnformatted(kDefaultLabel);
    } else {
        const std::string name = applied + " " + fonts.appliedBodyFace().style;
        ImGui::TextWrapped("%s", name.c_str());
    }
    ImGui::TextWrapped("%s", kSampleLine);
    ImGui::PushFont(fonts.emphasisFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextWrapped("%s", kBoldSampleLine);
    ImGui::PopFont();

    ImGui::End();
}
