#include "signal_generator_widget.h"
#include "common.h"
#include "imgui.h"
#include "logging_core.h"
#include "utils.h"
#include <string>

SignalGeneratorWidget::SignalGeneratorWidget(SignalGeneratorEngine &engine) : m_engine(engine) {}

void SignalGeneratorWidget::draw(const char *title, bool *p_open) {
    if (ImGui::Begin(title, p_open)) {
        if (ImGui::Checkbox("Measure", &m_engine.node().view_enabled)) {
            LOG_INFO("Change measurement active state [gen%d -> %s].", m_engine.id(),
                     m_engine.node().view_enabled ? "True" : "False");
        }

        ImGui::SeparatorText("Tones");

        if (ImGui::BeginTable("tones", 4, ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("Frequency (MHz)");
            ImGui::TableSetupColumn("Amplitude (dBm)");
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableHeadersRow();

            int to_delete = -1;

            for (int i = 0; i < static_cast<int>(m_engine.toneCount()); ++i) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", i + 1);

                ImGui::TableNextColumn();
                double freq = m_engine.tones()[static_cast<size_t>(i)].freq_Hz;
                ImGui::PushID(("freq" + std::to_string(i)).c_str());
                bool freq_changed = utils::inputFrequency("##freq", freq, 1.0, 100.0, "%.0f",
                                                          MIN_FREQ, MAX_FREQ);
                ImGui::PopID();

                ImGui::TableNextColumn();
                double amp = m_engine.tones()[static_cast<size_t>(i)].power_dBm;
                ImGui::PushID(("amp" + std::to_string(i)).c_str());
                bool amp_changed = utils::inputDouble("##amp", amp, 1, 5, "%.0f",
                                                      MIN_POWER, MAX_POWER);
                ImGui::PopID();

                if (freq_changed || amp_changed) {
                    m_engine.updateTone(static_cast<size_t>(i), freq, amp);
                    if (freq_changed) {
                        LOG_INFO("Update tone frequency: [gen%d tone%d -> %.0f MHz].",
                                 m_engine.id(), i, freq / 1e6);
                    }
                    if (amp_changed) {
                        LOG_INFO("Update tone amplitude: [gen%d tone%d -> %.0f dBm].",
                                 m_engine.id(), i, amp);
                    }
                }

                ImGui::TableNextColumn();
                ImGui::PushID(("del" + std::to_string(i)).c_str());
                if (ImGui::SmallButton("X")) {
                    to_delete = i;
                }
                ImGui::PopID();
            }

            ImGui::EndTable();

            if (to_delete >= 0) {
                m_engine.removeTone(static_cast<size_t>(to_delete));
                LOG_INFO("Remove tone: [gen%d tone%d].", m_engine.id(), to_delete);
            }
        }

        if (ImGui::Button("+ Add Tone")) {
            m_engine.addTone(100e6, -60.0);
            LOG_INFO("Add tone: [gen%d -> 100 MHz, -60 dBm].", m_engine.id());
        }

        ImGui::End();
    }
}
