#include "spectrum_analyzer_widget.h"
#include "common.h"
#include "imgui.h"
#include "implot.h"
#include "logging_core.h"
#include "utils.h"
#include "view_manager.h"
#include "pfb_channelizer_engine.h"
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
        const auto& spec = (m_pfb_ptr && node == &m_pfb_ptr->node())
            ? node->outputs[1] : node->outputs[0];
        if (!freq_axis && !spec.frequencies.empty())
            freq_axis = &spec.frequencies;
    }

    if (!freq_axis) {
        ImGui::Text("Unable to display spectrum (no frequency grid).");
        ImGui::End();
        return;
    }

    // Build combined specs for marker + avg noise
    std::vector<const Spectrum*> specs;
    for (auto* node : active_nodes) {
        if (!node) continue;
        specs.push_back(m_pfb_ptr && node == &m_pfb_ptr->node()
            ? &node->outputs[1] : &node->outputs[0]);
    }

    // Render combined spectrum (for marker + noise readout)
    std::vector<double> combined_dBm = m_engine.renderCombinedSpectrum(specs);
    if (combined_dBm.size() != freq_axis->size()) {
        ImGui::Text("Unable to render combined spectrum.");
        ImGui::End();
        return;
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
            auto* node = active_nodes[i];
            if (!node) continue;
            bool is_pfb = (m_pfb_ptr && node == &m_pfb_ptr->node());
            const auto& spec = is_pfb ? node->outputs[1] : node->outputs[0];

            std::vector<double> trace = m_engine.renderSpectrum(spec);
            if (trace.size() != freq_axis->size()) continue;

            std::string label = (i < m_probe_labels.size()) ? m_probe_labels[i]
                              : ("Probe " + std::to_string(i));
            ImPlot::PlotLine(label.c_str(), freq_axis->data(), trace.data(),
                             static_cast<int>(trace.size()),
                             {ImPlotProp_LineColor, trace_colors[i % 4],
                              ImPlotProp_LineWeight, 1.5f});

            // For PFB: overlay active channel highlight trace
            if (is_pfb) {
                double ch_center = m_pfb_ptr->activeChannelCenter_Hz();
                double ch_bw = m_pfb_ptr->activeChannelBandwidth_Hz();
                double ch_lo = ch_center - ch_bw / 2.0;
                double ch_hi = ch_center + ch_bw / 2.0;

                std::vector<double> highlight_freqs;
                std::vector<double> highlight_data;
                for (size_t j = 0; j < trace.size() && j < freq_axis->size(); ++j) {
                    if ((*freq_axis)[j] >= ch_lo && (*freq_axis)[j] <= ch_hi) {
                        highlight_freqs.push_back((*freq_axis)[j]);
                        highlight_data.push_back(trace[j]);
                    }
                }
                if (!highlight_data.empty()) {
                    ImPlot::PlotLine("Active Ch", highlight_freqs.data(), highlight_data.data(),
                                     static_cast<int>(highlight_data.size()),
                                     {ImPlotProp_LineColor, ImVec4(0.90f, 0.59f, 0.16f, 1.0f),
                                      ImPlotProp_LineWeight, 2.5f});
                }
            }
        }

        // Drag-to-zoom
        if (ImPlot::IsPlotHovered()) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImPlotPoint p = ImPlot::GetPlotMousePos();
                m_zoom.active = true;
                m_zoom.start_freq_Hz = p.x;
                m_zoom.end_freq_Hz = p.x;
            }
            if (m_zoom.active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                ImPlotPoint p = ImPlot::GetPlotMousePos();
                m_zoom.end_freq_Hz = p.x;
            }
        }

        // Draw selection rectangle during drag
        if (m_zoom.active) {
            double x1 = m_zoom.start_freq_Hz;
            double x2 = m_zoom.end_freq_Hz;
            if (std::abs(x2 - x1) > 1.0) {
                double y1 = m_engine.minPower();
                double y2 = m_engine.maxPower();
                ImPlotPoint p1 = ImPlot::PlotToPixels(x1, y1);
                ImPlotPoint p2 = ImPlot::PlotToPixels(x2, y2);
                ImPlot::GetPlotDrawList()->AddRectFilled(
                    ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y),
                    IM_COL32(100, 150, 255, 40));
            }
        }

        // Marker display (no drag)
        if (m_marker.enabled && !combined_dBm.empty()) {
            int idx = resolveMarkerIdx(*freq_axis, combined_dBm);
            if (idx >= 0 && idx < static_cast<int>(combined_dBm.size())) {
                double mf = (*freq_axis)[static_cast<size_t>(idx)];
                double mp = combined_dBm[static_cast<size_t>(idx)];
                drawMarkerOnPlot(mf, mp);
            }
        }

        ImPlot::EndPlot();
    }

    // Apply zoom on release
    if (m_zoom.active && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_zoom.active = false;
        double f1 = m_zoom.start_freq_Hz;
        double f2 = m_zoom.end_freq_Hz;
        double lo = std::min(f1, f2);
        double hi = std::max(f1, f2);
        if (hi - lo > 1.0) {
            m_engine.setStartFrequency(lo);
            m_engine.setStopFrequency(hi);
        }
    }

    double avg_noise = m_engine.computeAverageNoiseLevel(specs);
    ImGui::Text("Average noise level: %.2f dBm", avg_noise);

    if (ImGui::Button("Reset Zoom")) {
        m_engine.setStartFrequency(MIN_FREQ);
        m_engine.setStopFrequency(MAX_FREQ);
    }

    drawMarkerControls(*freq_axis, combined_dBm);

    ImGui::End();
}
