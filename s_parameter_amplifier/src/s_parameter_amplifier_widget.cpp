#include "s_parameter_amplifier_widget.h"
#include "imgui.h"
#include "implot.h"
#include "logging_core.h"
#include <cmath>
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

        // S21 magnitude plot
        bool any_loaded = false;
        for (const auto& engine : m_engines) {
            if (engine->loaded()) {
                any_loaded = true;
                break;
            }
        }

        if (any_loaded) {
            ImGui::SeparatorText("|S21| (dB)");
            if (ImPlot::BeginPlot("S21 Plot", ImVec2(-1, 250))) {
                ImPlot::SetupAxes("Frequency (GHz)", "|S21| (dB)",
                                  ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);

                for (size_t i = 0; i < m_engines.size(); ++i) {
                    const auto& engine = *m_engines[i];
                    if (!engine.loaded() || engine.freqs().empty()) continue;

                    const auto& freqs = engine.freqs();
                    const auto& params = engine.params();
                    size_t fwd_idx = static_cast<size_t>(engine.forwardParamIdx());

                    std::vector<double> freqs_ghz;
                    std::vector<double> mags_db;
                    freqs_ghz.reserve(freqs.size());
                    mags_db.reserve(params.size());
                    for (size_t j = 0; j < freqs.size(); ++j) {
                        freqs_ghz.push_back(freqs[j] / 1e9);
                        mags_db.push_back(20.0 * std::log10(std::abs(params[j][fwd_idx])));
                    }

                    std::string label = "Amp " + std::to_string(i + 1);
                    ImPlot::PlotLine(label.c_str(), freqs_ghz.data(), mags_db.data(),
                                     static_cast<int>(freqs_ghz.size()));
                }

                ImPlot::EndPlot();
            }
        }

        ImGui::End();
    }
}
