#include "mixer_widget.h"
#include "imgui.h"
#include "logging_core.h"
#include "utils.h"
#include <string>

MixerWidget::MixerWidget(std::vector<std::unique_ptr<MixerEngine>>& engines)
    : m_engines(engines) {}

void MixerWidget::draw(const char* title, bool* p_open) {
    if (ImGui::Begin(title, p_open)) {
        ImGui::SeparatorText("Mixers");

        if (ImGui::BeginTable("mixers", 4, ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("LO Freq (MHz)");
            ImGui::TableSetupColumn("Conv Gain (dB)");
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableHeadersRow();

            int to_delete = -1;

            for (int i = 0; i < static_cast<int>(m_engines.size()); ++i) {
                MixerEngine& engine = *m_engines[static_cast<size_t>(i)];

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", i + 1);

                ImGui::TableNextColumn();
                double lo = engine.loFreq_Hz();
                ImGui::PushID(("lo" + std::to_string(i)).c_str());
                bool lo_changed = utils::inputFrequency("##lo", lo, 1.0, 10.0, "%.3f", 1e6, 10e9);
                ImGui::PopID();

                ImGui::TableNextColumn();
                double gain = engine.conversionGain_dB();
                ImGui::PushID(("gain" + std::to_string(i)).c_str());
                bool gain_changed = utils::inputDouble("##gain", gain, 1, 10, "%.1f", -30.0, 30.0);
                ImGui::PopID();

                if (lo_changed) {
                    engine.setLoFreq_Hz(lo);
                    LOG_INFO("Update mixer LO: [mix%d -> %.3f MHz]", engine.id(), lo / 1e6);
                }
                if (gain_changed) {
                    engine.setConversionGain_dB(gain);
                    LOG_INFO("Update mixer gain: [mix%d -> %.1f dB]", engine.id(), gain);
                }

                ImGui::TableNextColumn();
                ImGui::PushID(("del" + std::to_string(i)).c_str());
                if (ImGui::SmallButton("X")) {
                    to_delete = i;
                }
                ImGui::PopID();
            }

            ImGui::EndTable();

            if (to_delete >= 0 && onRemoveMixer) {
                onRemoveMixer(static_cast<size_t>(to_delete));
                LOG_INFO("Remove mixer: [mix%d].", to_delete);
            }
        }

        if (ImGui::Button("+ Add Mixer") && onAddMixer) {
            onAddMixer();
            LOG_INFO("Add mixer.");
        }

        ImGui::End();
    }
}
