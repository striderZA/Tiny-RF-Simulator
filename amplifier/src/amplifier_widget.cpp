#include "amplifier_widget.h"
#include "imgui.h"
#include "logging_core.h"
#include "utils.h"
#include <string>

AmplifierWidget::AmplifierWidget(std::vector<std::unique_ptr<AmplifierEngine>> &engines)
    : m_engines(engines) {}

void AmplifierWidget::draw(const char *title, bool *p_open) {
    if (ImGui::Begin(title, p_open)) {
        ImGui::SeparatorText("Amplifiers");

        if (ImGui::BeginTable("amps", 4, ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("Gain (dB)");
            ImGui::TableSetupColumn("NF (dB)");
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableHeadersRow();

            int to_delete = -1;

            for (int i = 0; i < static_cast<int>(m_engines.size()); ++i) {
                AmplifierEngine &engine = *m_engines[static_cast<size_t>(i)];

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", i + 1);

                ImGui::TableNextColumn();
                double gain = engine.gain_dB();
                ImGui::PushID(("gain" + std::to_string(i)).c_str());
                bool gain_changed = utils::inputDouble("##gain", gain, 1, 10, "%.1f", -10.0, 40.0);
                ImGui::PopID();

                ImGui::TableNextColumn();
                double nf = engine.nf_dB();
                ImGui::PushID(("nf" + std::to_string(i)).c_str());
                bool nf_changed = utils::inputDouble("##nf", nf, 0.1, 10, "%.1f", 0.0, 30.0);
                ImGui::PopID();

                if (gain_changed) {
                    engine.setGain_dB(gain);
                    LOG_INFO("Update amplifier gain: [amp%d -> %.1f dB]", engine.id(), gain);
                }
                if (nf_changed) {
                    engine.setNF_dB(nf);
                    LOG_INFO("Update amplifier NF: [amp%d -> %.1f dB]", engine.id(), nf);
                }

                ImGui::TableNextColumn();
                ImGui::PushID(("del" + std::to_string(i)).c_str());
                if (ImGui::SmallButton("X")) {
                    to_delete = i;
                }
                ImGui::PopID();
            }

            ImGui::EndTable();

            if (to_delete >= 0 && onRemoveAmplifier) {
                onRemoveAmplifier(static_cast<size_t>(to_delete));
                LOG_INFO("Remove amplifier: [amp%d].", to_delete);
            }
        }

        if (ImGui::Button("+ Add Amplifier") && onAddAmplifier) {
            onAddAmplifier();
            LOG_INFO("Add amplifier.");
        }

        ImGui::End();
    }
}
