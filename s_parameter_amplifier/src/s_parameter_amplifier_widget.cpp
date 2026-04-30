#include "s_parameter_amplifier_widget.h"
#include "imgui.h"
#include "logging_core.h"
#include <string>

SParameterAmplifierWidget::SParameterAmplifierWidget(
    std::vector<std::unique_ptr<SParameterAmplifierEngine>>& engines)
    : m_engines(engines) {}

void SParameterAmplifierWidget::draw(const char* title, bool* p_open) {
    if (ImGui::Begin(title, p_open)) {
        ImGui::SeparatorText("S-Parameter Amplifiers");

        if (ImGui::BeginTable("sparam_amps", 3, ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("File");
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableHeadersRow();

            int to_delete = -1;

            for (int i = 0; i < static_cast<int>(m_engines.size()); ++i) {
                SParameterAmplifierEngine& engine = *m_engines[static_cast<size_t>(i)];

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", i + 1);

                ImGui::TableNextColumn();
                const char* status = engine.loaded() ? "" : " (failed)";
                ImGui::Text("%s%s", engine.filepath().c_str(), status);

                ImGui::TableNextColumn();
                ImGui::PushID(("del" + std::to_string(i)).c_str());
                if (ImGui::SmallButton("X")) {
                    to_delete = i;
                }
                ImGui::PopID();
            }

            ImGui::EndTable();

            if (to_delete >= 0 && onRemoveSParamAmp) {
                onRemoveSParamAmp(static_cast<size_t>(to_delete));
                LOG_INFO("Remove S-parameter amplifier: [spamp%d].", to_delete);
            }
        }

        if (ImGui::Button("+ Add S-Param Amp") && onAddSParamAmp) {
            onAddSParamAmp();
            LOG_INFO("Add S-parameter amplifier.");
        }

        ImGui::End();
    }
}
