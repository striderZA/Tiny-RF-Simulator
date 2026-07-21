#include "library_browser_widget.h"
#include "component_library.h"
#include <imgui.h>
#include <cstring>
#include <algorithm>
#include <map>

LibraryBrowserWidget::LibraryBrowserWidget(ComponentLibrary& library)
    : m_library(&library) {}

bool LibraryBrowserWidget::matchesFilter(const ComponentDefinition& def) const {
    if (m_filter_buffer[0] == '\0') return true;
    std::string filter = m_filter_buffer;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
    auto contains = [&](const std::string& s) {
        std::string lower = s;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return lower.find(filter) != std::string::npos;
    };
    return contains(def.part_number) || contains(def.manufacturer) || contains(def.description);
}

void LibraryBrowserWidget::draw(const char* title, bool* p_open) {
    if (!ImGui::Begin(title, p_open)) { ImGui::End(); return; }

    ImGui::InputTextWithHint("##filter", "Filter by part number, manufacturer, or description",
                             m_filter_buffer, sizeof(m_filter_buffer));
    ImGui::Separator();

    auto all_defs = m_library->all();

    // Group by type, then manufacturer
    std::map<std::string, std::map<std::string, std::vector<const ComponentDefinition*>>> grouped;
    for (auto* def : all_defs) {
        if (!matchesFilter(*def)) continue;
        grouped[def->type][def->manufacturer].push_back(def);
    }

    if (grouped.empty()) {
        ImGui::TextDisabled("No components found.");
        if (all_defs.empty())
            ImGui::TextDisabled("Add .json files to ~/.rf-sim/libraries/");
    } else {
        for (auto& [type, manufacturers] : grouped) {
            if (ImGui::TreeNode(type.c_str())) {
                for (auto& [manufacturer, defs] : manufacturers) {
                    std::string mfg_label = manufacturer.empty() ? "(Unknown)" : manufacturer;
                    mfg_label += " (" + std::to_string(defs.size()) + ")";
                    if (ImGui::TreeNode(mfg_label.c_str())) {
                        for (auto* def : defs) {
                            std::string item_label = def->part_number;
                            if (!def->description.empty())
                                item_label += "  " + def->description;
                            if (ImGui::Selectable(item_label.c_str())) {
                                if (onInsert) onInsert(*def);
                            }
                            if (ImGui::IsItemHovered()) {
                                std::string tooltip = "Part: " + def->part_number + "\n";
                                if (!def->manufacturer.empty())
                                    tooltip += "Manufacturer: " + def->manufacturer + "\n";
                                tooltip += "Type: " + def->type + "\n";
                                if (!def->notes.empty())
                                    tooltip += "Notes: " + def->notes + "\n";
                                tooltip += "Source: " + def->source_path;
                                ImGui::SetTooltip("%s", tooltip.c_str());
                            }
                        }
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
            }
        }
    }
    ImGui::End();
}
