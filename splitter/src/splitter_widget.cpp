#include "splitter_widget.h"
#include "imgui.h"
#include "logging_core.h"
#include <string>

SplitterWidget::SplitterWidget(std::vector<std::unique_ptr<SplitterEngine>>& engines)
    : m_engines(engines) {}

void SplitterWidget::draw(const char* title, bool* p_open) {
    if (ImGui::Begin(title, p_open)) {
        ImGui::SeparatorText("Splitters");

        if (ImGui::BeginTable("splitters", 2, ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableHeadersRow();

            int to_delete = -1;

            for (int i = 0; i < static_cast<int>(m_engines.size()); ++i) {
                SplitterEngine& engine = *m_engines[static_cast<size_t>(i)];

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", i + 1);

                ImGui::TableNextColumn();
                ImGui::PushID(("del" + std::to_string(i)).c_str());
                if (ImGui::SmallButton("X")) {
                    to_delete = i;
                }
                ImGui::PopID();
            }

            ImGui::EndTable();

            if (to_delete >= 0 && onRemoveSplitter) {
                onRemoveSplitter(static_cast<size_t>(to_delete));
                LOG_INFO("Remove splitter: [split%d].", to_delete);
            }
        }

        if (ImGui::Button("+ Add Splitter") && onAddSplitter) {
            onAddSplitter();
            LOG_INFO("Add splitter.");
        }

        ImGui::End();
    }
}
