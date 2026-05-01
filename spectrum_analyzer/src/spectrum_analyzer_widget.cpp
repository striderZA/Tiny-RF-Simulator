#include "spectrum_analyzer_widget.h"
#include "common.h"
#include "imgui.h"
#include "implot.h"
#include "logging_core.h"
#include "utils.h"
#include "view_manager.h"
#include <algorithm>
#include <limits>

SpectrumAnalyzerWidget::SpectrumAnalyzerWidget(SpectrumAnalyzerEngine &engine, ViewManager &vm)
    : m_engine(engine), m_view_manager(vm) {}

int SpectrumAnalyzerWidget::resolveMarkerIdx(const std::vector<double> &freq_axis,
                                              const std::vector<double> &data) const {
    if (!m_marker.enabled || data.empty() || freq_axis.empty()) {
        return -1;
    }

    // Try to find nearest cached peak to target_freq_Hz
    if (!m_marker.peaks.empty()) {
        int best_idx = -1;
        double best_dist = std::numeric_limits<double>::max();
        for (const auto &pk : m_marker.peaks) {
            double dist = std::abs(pk.freq_Hz - m_marker.target_freq_Hz);
            if (dist < best_dist) {
                best_dist = dist;
                best_idx = pk.index;
            }
        }
        if (best_idx >= 0 && best_idx < static_cast<int>(data.size())) {
            return best_idx;
        }
    }

    // Fallback: nearest bin to target_freq_Hz
    int best_idx = 0;
    double best_dist = std::numeric_limits<double>::max();
    for (size_t i = 0; i < freq_axis.size(); ++i) {
        double dist = std::abs(freq_axis[i] - m_marker.target_freq_Hz);
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = static_cast<int>(i);
        }
    }
    return best_idx;
}

void SpectrumAnalyzerWidget::drawMarkerOnPlot(double freq_Hz, double power_dBm) {
    ImPlotSpec marker_spec;
    marker_spec.Marker = ImPlotMarker_Down;
    marker_spec.MarkerSize = 8.0f;
    marker_spec.MarkerFillColor = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
    marker_spec.LineWeight = 1.0f;
    marker_spec.MarkerLineColor = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
    ImPlot::PlotScatter("Marker", &freq_Hz, &power_dBm, 1, marker_spec);
}

void SpectrumAnalyzerWidget::drawMarkerControls(const std::vector<double> &freq_axis,
                                                const std::vector<double> &display_dBm) {
    if (ImGui::Checkbox("Enable Marker", &m_marker.enabled)) {
        if (m_marker.enabled && !freq_axis.empty()) {
            m_marker.target_freq_Hz = freq_axis[freq_axis.size() / 2];
            m_marker.peaks.clear();
        }
    }

    if (!m_marker.enabled) {
        return;
    }

    if (ImGui::Button("Snap to Peak")) {
        m_marker.peaks = m_engine.findPeaks(display_dBm, freq_axis, 8);
        if (!m_marker.peaks.empty()) {
            m_marker.target_freq_Hz = m_marker.peaks[0].freq_Hz;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Next Peak")) {
        if (!m_marker.peaks.empty()) {
            // Find the currently tracked peak (nearest to target_freq_Hz)
            int current_rank = 0;
            double best_dist = std::numeric_limits<double>::max();
            for (size_t i = 0; i < m_marker.peaks.size(); ++i) {
                double dist = std::abs(m_marker.peaks[i].freq_Hz - m_marker.target_freq_Hz);
                if (dist < best_dist) {
                    best_dist = dist;
                    current_rank = static_cast<int>(i);
                }
            }
            int next_rank = (current_rank + 1) % static_cast<int>(m_marker.peaks.size());
            m_marker.target_freq_Hz = m_marker.peaks[next_rank].freq_Hz;
        }
    }

    int idx = resolveMarkerIdx(freq_axis, display_dBm);
    double marker_freq = 0.0;
    double marker_power = -174.0;
    if (idx >= 0 && idx < static_cast<int>(display_dBm.size())) {
        marker_freq = freq_axis[static_cast<size_t>(idx)];
        marker_power = display_dBm[static_cast<size_t>(idx)];
    }
    ImGui::Text("Marker: %.2f MHz, %.2f dBm", marker_freq / 1e6, marker_power);
}

void SpectrumAnalyzerWidget::draw(const char *title, bool *p_open) {
    ImGui::SetNextWindowSize(ImVec2(500, 550), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(400, 400), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    double s = m_engine.startFrequency();
    double e = m_engine.stopFrequency();
    double vbw = m_engine.vbw();
    double rbw = m_engine.rbw();
    double minp = m_engine.minPower();
    double maxp = m_engine.maxPower();

    if (utils::inputFrequency("Start Frequency (MHz)", s, 1.0, 100.0, "%.0f", MIN_FREQ, MAX_FREQ)) {
        m_engine.setStartFrequency(s);
        LOG_INFO("Update start frequency: %.0f MHz", m_engine.startFrequency() / 1e6);
    }

    if (utils::inputFrequency("Stop Frequency (MHz)", e, 1.0, 100.0, "%.0f", MIN_FREQ, MAX_FREQ)) {
        m_engine.setStopFrequency(e);
        LOG_INFO("Update stop frequency: %.0f MHz", m_engine.stopFrequency() / 1e6);
    }

    if (utils::inputFrequency("VBW (MHz)", vbw, 1.0, 10.0, "%.0f", 1e6, 100e6)) {
        m_engine.setVideoBw(vbw);
        LOG_INFO("Update VBW: %.0f MHz", m_engine.vbw() / 1e6);
    }

    if (utils::inputFrequency("RBW (MHz)", rbw, 1.0, 10.0, "%.0f", 1e6, 100e6)) {
        m_engine.setResBw(rbw);
        LOG_INFO("Update RBW: %.0f MHz", m_engine.rbw() / 1e6);
    }

    if (utils::inputDouble("Ref (dBm)", maxp, 5, 10, "%.0f", MIN_POWER, MAX_POWER)) {
        m_engine.setMaxPower(maxp);
        LOG_INFO("Update ref power: %.0f dBm", m_engine.maxPower());
    }

    if (utils::inputDouble("Min level (dBm)", minp, 5, 10, "%.0f", MIN_POWER, MAX_POWER)) {
        m_engine.setMinPower(minp);
        LOG_INFO("Update min power: %.0f dBm", m_engine.minPower());
    }

    if (m_probe_labels.empty()) {
        ImGui::Text("No node probed");
    } else {
        std::string probes = "Probes:";
        for (size_t i = 0; i < m_probe_labels.size(); ++i)
            probes += "  [" + std::to_string(i) + "] " + m_probe_labels[i];
        ImGui::TextUnformatted(probes.c_str());
    }
    ImGui::Separator();

    auto active_nodes = m_view_manager.getActiveNodes();

    if (active_nodes.empty()) {
        ImGui::Text("No signal node selected for display!");
        ImGui::End();
        return;
    }

    const std::vector<double>* freq_axis = nullptr;
    for (auto* node : active_nodes) {
        if (!node) continue;
        if (!freq_axis && !node->outputs[0].frequencies.empty())
            freq_axis = &node->outputs[0].frequencies;
    }

    if (!freq_axis) {
        ImGui::Text("Unable to display spectrum (no frequency grid).");
        ImGui::End();
        return;
    }

    // Build combined specs for marker + avg noise
    std::vector<const Spectrum*> specs;
    for (auto* node : active_nodes)
        if (node) specs.push_back(&node->outputs[0]);

    std::vector<double> combined_dBm = m_engine.renderCombinedSpectrum(specs);
    if (combined_dBm.size() != freq_axis->size()) {
        ImGui::Text("Unable to render combined spectrum.");
        ImGui::End();
        return;
    }

    // Clear drag state if mouse released
    if (m_marker.is_dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_marker.is_dragging = false;
        m_marker.peaks = m_engine.findPeaks(combined_dBm, *freq_axis, 8);
    }

    static const ImVec4 trace_colors[4] = {
        ImVec4(0.09f, 0.78f, 0.60f, 1.0f),  // Teal
        ImVec4(0.90f, 0.59f, 0.16f, 1.0f),  // Orange
        ImVec4(0.47f, 0.20f, 0.67f, 1.0f),  // Purple
        ImVec4(0.24f, 0.55f, 0.86f, 1.0f),  // Blue
    };

    ImPlot::SetNextAxesLimits(m_engine.startFrequency(), m_engine.stopFrequency(),
                              m_engine.minPower(), m_engine.maxPower(), ImPlotCond_Always);

    if (ImPlot::BeginPlot("Spectrum")) {
        for (size_t i = 0; i < active_nodes.size(); ++i) {
            std::vector<double> trace = m_engine.renderSpectrum(active_nodes[i]->outputs[0]);
            if (trace.size() != freq_axis->size()) continue;
            std::string label = (i < m_probe_labels.size()) ? m_probe_labels[i]
                              : ("Probe " + std::to_string(i));
            ImPlot::PlotLine(label.c_str(), freq_axis->data(), trace.data(),
                             static_cast<int>(trace.size()),
                             {ImPlotProp_LineColor, trace_colors[i % 4],
                              ImPlotProp_LineWeight, 1.5f});
        }

        if (m_marker.enabled && !combined_dBm.empty()) {
            if (ImPlot::IsPlotHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                m_marker.is_dragging = true;
                ImPlotPoint mouse_pos = ImPlot::GetPlotMousePos();
                m_marker.target_freq_Hz = mouse_pos.x;
            }

            int idx = resolveMarkerIdx(*freq_axis, combined_dBm);
            if (idx >= 0 && idx < static_cast<int>(combined_dBm.size())) {
                double mf = (*freq_axis)[static_cast<size_t>(idx)];
                double mp = combined_dBm[static_cast<size_t>(idx)];
                drawMarkerOnPlot(mf, mp);
            }
        }

        ImPlot::EndPlot();
    }

    double avg_noise = m_engine.computeAverageNoiseLevel(specs);
    ImGui::Text("Average noise level: %.2f dBm", avg_noise);

    drawMarkerControls(*freq_axis, combined_dBm);

    ImGui::End();
}
