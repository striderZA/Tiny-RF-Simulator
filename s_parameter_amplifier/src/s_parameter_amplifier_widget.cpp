#include "s_parameter_amplifier_widget.h"
#include "imgui.h"
#include "implot.h"
#include "logging_core.h"
#include "portable-file-dialogs.h"
#include <cmath>
#include <string>
#include <vector>

static std::string paramLabel(int num_ports, int idx) {
    int p = idx / num_ports;
    int q = idx % num_ports;
    return "S" + std::to_string(p + 1) + std::to_string(q + 1);
}

SParameterAmplifierWidget::SParameterAmplifierWidget(
    std::vector<std::unique_ptr<SParameterAmplifierEngine>>& engines)
    : m_engines(engines) {}

void SParameterAmplifierWidget::draw(const char* title, bool* p_open) {
    if (ImGui::Begin(title, p_open)) {
        ImGui::SeparatorText("S-Parameter Amplifiers");

        // ---- Table ----
        if (ImGui::BeginTable("sparam_amps", 5, ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 25.0f);
            ImGui::TableSetupColumn("File");
            ImGui::TableSetupColumn("Fwd", ImGuiTableColumnFlags_WidthFixed, 75.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableHeadersRow();

            int to_delete = -1;

            for (int i = 0; i < static_cast<int>(m_engines.size()); ++i) {
                auto& engine = *m_engines[static_cast<size_t>(i)];
                auto idx = static_cast<size_t>(i);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", i + 1);

                ImGui::TableNextColumn();
                const char* status = engine.loaded() ? "" : " (failed)";
                ImGui::Text("%s%s", engine.filepath().c_str(), status);

                // Forward param combo
                ImGui::TableNextColumn();
                if (engine.loaded()) {
                    int np = engine.numPorts();
                    int current = engine.forwardParamIdx();
                    std::string preview = paramLabel(np, current);
                    ImGui::PushID(("fwd" + std::to_string(i)).c_str());
                    if (ImGui::BeginCombo("##fwd", preview.c_str())) {
                        for (int pi = 0; pi < np * np; ++pi) {
                            std::string label = paramLabel(np, pi);
                            bool selected = (pi == current);
                            if (ImGui::Selectable(label.c_str(), selected))
                                engine.setForwardParamIdx(pi);
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                } else {
                    ImGui::TextDisabled("-");
                }

                // Browse button
                ImGui::TableNextColumn();
                ImGui::PushID(("browse" + std::to_string(i)).c_str());
                if (ImGui::SmallButton("...")) {
                    auto result = pfd::open_file("Select S-parameter file", "",
                        { "S-parameter Files", "*.s2p *.s3p *.s4p *.sNp" }).result();
                    if (!result.empty()) {
                        engine.reload(result[0]);
                        LOG_INFO("S-param amp %d reloaded: %s", i, result[0].c_str());
                    }
                }
                ImGui::PopID();

                // Delete button
                ImGui::TableNextColumn();
                ImGui::PushID(("del" + std::to_string(i)).c_str());
                if (ImGui::SmallButton("X"))
                    to_delete = i;
                ImGui::PopID();
            }

            ImGui::EndTable();

            if (to_delete >= 0 && onRemoveSParamAmp) {
                size_t del = static_cast<size_t>(to_delete);
                onRemoveSParamAmp(del);
                LOG_INFO("Remove S-parameter amplifier: [spamp%d].", to_delete);
                // Remove visibility entry and renumber subsequent ones
                m_param_visible.erase(del);
                for (auto it = m_param_visible.begin(); it != m_param_visible.end();) {
                    if (it->first > del) {
                        m_param_visible[it->first - 1] = std::move(it->second);
                        it = m_param_visible.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }

        // ---- Add button ----
        if (ImGui::Button("+ Add S-Param Amp") && onAddSParamAmp) {
            onAddSParamAmp();
        }

        // ---- Parameter visibility trees ----
        for (size_t i = 0; i < m_engines.size(); ++i) {
            const auto& engine = *m_engines[i];
            if (!engine.loaded()) continue;

            auto& vis = m_param_visible[i];
            int np = engine.numPorts();
            int total = np * np;
            if (vis.size() != static_cast<size_t>(total)) {
                vis.assign(total, false);
                if (total > 0) vis[np] = true;
            }

            std::string node_label = "Amp " + std::to_string(i + 1) + "##vis" + std::to_string(i);
            if (ImGui::TreeNode(node_label.c_str())) {
                int ncols = (np < 3) ? 4 : np;
                for (int pi = 0; pi < total; ++pi) {
                    if (pi % ncols != 0) ImGui::SameLine();
                    std::string label = paramLabel(np, pi) + "##c" + std::to_string(i) + "_" + std::to_string(pi);
                    bool b = vis[pi];
                    if (ImGui::Checkbox(label.c_str(), &b))
                        vis[pi] = b;
                }
                ImGui::TreePop();
            }
        }

        // ---- Combined plot ----
        bool has_visible = false;
        for (size_t i = 0; i < m_engines.size(); ++i) {
            if (!m_engines[i]->loaded()) continue;
            auto it = m_param_visible.find(i);
            if (it == m_param_visible.end()) continue;
            for (bool v : it->second) {
                if (v) { has_visible = true; break; }
            }
            if (has_visible) break;
        }

        if (has_visible) {
            ImGui::SeparatorText("S-Parameters (dB)");
            if (ImPlot::BeginPlot("S-Param Plot", ImVec2(-1, 300))) {
                ImPlot::SetupAxes("Frequency (GHz)", "Magnitude (dB)",
                                  ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);

                static const ImVec4 palette[16] = {
                    {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.5f, 0.0f, 1.0f},
                    {0.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 0.65f, 0.0f, 1.0f},
                    {0.5f, 0.0f, 0.5f, 1.0f}, {0.0f, 0.75f, 0.75f, 1.0f},
                    {0.75f, 0.75f, 0.0f, 1.0f}, {0.5f, 0.5f, 0.5f, 1.0f},
                    {1.0f, 0.0f, 0.5f, 1.0f}, {0.0f, 1.0f, 0.5f, 1.0f},
                    {0.5f, 0.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f},
                    {0.75f, 0.25f, 0.0f, 1.0f}, {0.25f, 0.5f, 0.0f, 1.0f},
                    {0.0f, 0.25f, 0.75f, 1.0f}, {1.0f, 0.5f, 0.75f, 1.0f},
                };

                int color_idx = 0;
                for (size_t i = 0; i < m_engines.size(); ++i) {
                    const auto& engine = *m_engines[i];
                    if (!engine.loaded()) continue;

                    auto it = m_param_visible.find(i);
                    if (it == m_param_visible.end()) continue;

                    const auto& freqs = engine.freqs();
                    int np = engine.numPorts();

                    std::vector<double> freqs_ghz;
                    freqs_ghz.reserve(freqs.size());
                    for (double f : freqs)
                        freqs_ghz.push_back(f / 1e9);

                    for (int pi = 0; pi < static_cast<int>(it->second.size()); ++pi) {
                        if (!it->second[pi]) continue;

                        std::vector<double> mags_db;
                        mags_db.reserve(freqs.size());
                        for (size_t fi = 0; fi < freqs.size(); ++fi) {
                            double mag = std::abs(engine.params()[fi][pi]);
                            mags_db.push_back(20.0 * std::log10((std::max)(mag, 1e-30)));
                        }

                        std::string label = "Amp" + std::to_string(i + 1) + " " + paramLabel(np, pi);
                        ImPlot::PlotLine(label.c_str(), freqs_ghz.data(), mags_db.data(),
                                         static_cast<int>(freqs_ghz.size()),
                                         {ImPlotProp_LineColor, palette[color_idx % 16]});
                        ++color_idx;
                    }
                }

                ImPlot::EndPlot();
            }
        }

        ImGui::End();
    }
}
