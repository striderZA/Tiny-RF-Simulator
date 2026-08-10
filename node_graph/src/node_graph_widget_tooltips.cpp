#include "imgui.h"
#include "imnodes.h"
#include "node_graph_widget.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace {

void showSpectrumTooltip(const Spectrum &spec, const char *direction) {
    ImGui::BeginTooltip();

    if (spec.frequencies.empty() && spec.tones.empty()) {
        ImGui::Text("%s: No signal", direction);
        ImGui::EndTooltip();
        return;
    }

    int num_tones = static_cast<int>(spec.tones.size());
    if (num_tones > 0) {
        double strongest_power = -std::numeric_limits<double>::infinity();
        double strongest_freq = 0.0;
        for (const auto &t : spec.tones) {
            if (t.power_dBm > strongest_power) {
                strongest_power = t.power_dBm;
                strongest_freq = t.freq_Hz;
            }
        }
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Tones: %d  |  Strongest: %.3f MHz @ %.1f dBm", num_tones,
                      strongest_freq / 1e6, strongest_power);
        ImGui::TextUnformatted(buf);
    } else {
        ImGui::Text("Tones: 0");
    }

    if (!spec.noise_total_W.empty()) {
        double sum = 0.0;
        for (double n : spec.noise_total_W)
            sum += n;
        double avg_W = sum / static_cast<double>(spec.noise_total_W.size());
        double avg_dBm_per_Hz = 10.0 * std::log10(avg_W) + 30.0;
        ImGui::Text("Noise floor: %.1f dBm/Hz", avg_dBm_per_Hz);
    } else {
        ImGui::Text("Noise floor: -- dBm/Hz");
    }

    if (!spec.frequencies.empty()) {
        double f_min = spec.frequencies.front();
        double f_max = spec.frequencies.back();
        double f_center = (f_min + f_max) / 2.0;

        auto fmt_freq = [](double hz) -> std::string {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.3f MHz", hz / 1e6);
            return buf;
        };
        ImGui::Text("Freq range: %s - %s (center: %s)", fmt_freq(f_min).c_str(),
                    fmt_freq(f_max).c_str(), fmt_freq(f_center).c_str());
    }

    ImGui::EndTooltip();
}

} // namespace

void NodeGraphWidget::showPinTooltips() {
    int hovered_pin;
    if (!ImNodes::IsPinHovered(&hovered_pin))
        return;

    for (const auto &node : m_engine.nodes()) {
        const auto *signal = node.signal_node;
        if (!signal)
            continue;

        for (size_t i = 0; i < node.input_pin_ids.size(); ++i) {
            if (node.input_pin_ids[i] != hovered_pin)
                continue;
            const Spectrum *spec = (i < signal->inputs.size()) ? signal->inputs[i] : nullptr;
            if (spec) {
                showSpectrumTooltip(*spec, "IN");
                return;
            }
        }

        for (size_t i = 0; i < node.output_pin_ids.size(); ++i) {
            if (node.output_pin_ids[i] != hovered_pin)
                continue;
            const Spectrum &spec = (i < signal->outputs.size()) ? signal->outputs[i] : Spectrum();
            showSpectrumTooltip(spec, "OUT");
            return;
        }
    }

    // Boundary pin tooltips (synthesized pins with ids >= 100000)
    if (hovered_pin >= 100000) {
        auto it = m_synth_pin_to_real_pin.find(hovered_pin);
        if (it != m_synth_pin_to_real_pin.end()) {
            int real_pin = it->second;
            for (const auto &node : m_engine.nodes()) {
                for (size_t i = 0; i < node.output_pin_ids.size(); ++i) {
                    if (node.output_pin_ids[i] == real_pin && node.signal_node) {
                        if (i < node.signal_node->outputs.size()) {
                            showSpectrumTooltip(node.signal_node->outputs[i], "OUT");
                            return;
                        }
                    }
                }
                for (size_t i = 0; i < node.input_pin_ids.size(); ++i) {
                    if (node.input_pin_ids[i] == real_pin && node.signal_node) {
                        const Spectrum *spec = (i < node.signal_node->inputs.size())
                                                   ? node.signal_node->inputs[i]
                                                   : nullptr;
                        if (spec) {
                            showSpectrumTooltip(*spec, "IN");
                            return;
                        }
                    }
                }
            }
        }
    }
}

void NodeGraphWidget::showNodeHoverTooltips() {
    if (!onNodeHover)
        return;

    for (const auto &node : m_engine.nodes()) {
        int hovered_node = -1;
        if (ImNodes::IsNodeHovered(&hovered_node) && hovered_node == node.node_id) {
            std::string summary = onNodeHover(node.node_id);
            if (!summary.empty()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted(summary.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            break;
        }
    }
}
