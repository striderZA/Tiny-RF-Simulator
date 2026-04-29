#include "spectrum_analyzer_widget.h"
#include "common.h"
#include "imgui.h"
#include "implot.h"
#include "logging_core.h"
#include "utils.h"
#include "view_manager.h"
#include <algorithm>

SpectrumAnalyzerWidget::SpectrumAnalyzerWidget(SpectrumAnalyzerEngine &engine, ViewManager &vm)
    : m_engine(engine), m_view_manager(vm) {}

MarkerInfo SpectrumAnalyzerWidget::resolveMarker(const std::vector<double> &freq_axis,
                                                  const std::vector<double> &power_dBm) const {
    MarkerInfo info;
    if (!m_marker.enabled || power_dBm.empty()) {
        return info;
    }

    if (m_marker.selected_peak_idx >= 0 &&
        m_marker.selected_peak_idx < static_cast<int>(m_marker.peaks.size())) {
        const auto &pk = m_marker.peaks[m_marker.selected_peak_idx];
        info.idx = pk.index;
        info.freq_Hz = pk.freq_Hz;
        info.power_dBm = pk.power_dBm;
    } else {
        info.idx = std::clamp(m_marker.manual_bin, 0, static_cast<int>(power_dBm.size()) - 1);
        info.freq_Hz = freq_axis[static_cast<size_t>(info.idx)];
        info.power_dBm = power_dBm[static_cast<size_t>(info.idx)];
    }
    return info;
}

void SpectrumAnalyzerWidget::drawMarkerOnPlot(const MarkerInfo &info) {
    ImPlotSpec marker_spec;
    marker_spec.Marker = ImPlotMarker_Down;
    marker_spec.MarkerSize = 8.0f;
    marker_spec.MarkerFillColor = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
    marker_spec.LineWeight = 1.0f;
    marker_spec.MarkerLineColor = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
    ImPlot::PlotScatter("Marker", &info.freq_Hz, &info.power_dBm, 1, marker_spec);
}

void SpectrumAnalyzerWidget::drawMarkerControls(const std::vector<double> &freq_axis,
                                                const std::vector<double> &display_dBm) {
    if (ImGui::Checkbox("Enable Marker", &m_marker.enabled)) {
        if (m_marker.enabled) {
            m_marker.manual_bin = static_cast<int>(display_dBm.size() / 2);
            m_marker.selected_peak_idx = -1;
        }
    }

    if (!m_marker.enabled) {
        return;
    }

    if (ImGui::Button("Snap to Peak")) {
        m_marker.peaks = m_engine.findPeaks(display_dBm, freq_axis, 8);
        m_marker.selected_peak_idx = m_marker.peaks.empty() ? -1 : 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Next Peak")) {
        if (!m_marker.peaks.empty()) {
            if (m_marker.selected_peak_idx == -1) {
                m_marker.selected_peak_idx = 0;
            } else {
                m_marker.selected_peak_idx =
                    (m_marker.selected_peak_idx + 1) % static_cast<int>(m_marker.peaks.size());
            }
        }
    }

    if (ImGui::Button("<")) {
        if (m_marker.manual_bin > 0) {
            --m_marker.manual_bin;
        }
        m_marker.selected_peak_idx = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button(">")) {
        if (m_marker.manual_bin < static_cast<int>(display_dBm.size()) - 1) {
            ++m_marker.manual_bin;
        }
        m_marker.selected_peak_idx = -1;
    }

    MarkerInfo info = resolveMarker(freq_axis, display_dBm);
    ImGui::Text("Marker: %.2f MHz, %.2f dBm", info.freq_Hz / 1e6, info.power_dBm);
}

void SpectrumAnalyzerWidget::draw(const char *title, bool *p_open) {
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

    if (!m_probe_label.empty()) {
        ImGui::Text("Probing: %s", m_probe_label.c_str());
    } else {
        ImGui::Text("No node probed");
    }
    ImGui::Separator();

    auto active_nodes = m_view_manager.getActiveNodes();

    if (active_nodes.empty()) {
        ImGui::Text("No signal node selected for display!");
        ImGui::End();
        return;
    }

    // Build list of Spectrum pointers for combined rendering
    std::vector<const Spectrum *> specs;
    specs.reserve(active_nodes.size());
    const std::vector<double> *freq_axis = nullptr;
    for (auto *node : active_nodes) {
        if (!node) {
            continue;
        }
        if (!freq_axis && !node->output.frequencies.empty()) {
            freq_axis = &node->output.frequencies;
        }
        specs.push_back(&node->output);
    }

    // Render the combined spectrum (power summed across inputs)
    std::vector<double> display_dBm = m_engine.renderCombinedSpectrum(specs);

    if (!freq_axis || display_dBm.empty() || freq_axis->size() != display_dBm.size()) {
        ImGui::Text("Unable to display combined spectrum (frequency grid mismatch).");
        ImGui::End();
        return;
    }

    ImPlot::SetNextAxesLimits(m_engine.startFrequency(), m_engine.stopFrequency(),
                              m_engine.minPower(), m_engine.maxPower(), ImPlotCond_Always);

    if (ImPlot::BeginPlot("Spectrum")) {
        ImPlot::PlotLine("Combined Spectrum", freq_axis->data(), display_dBm.data(),
                         (int)display_dBm.size());

        if (m_marker.enabled && !display_dBm.empty()) {
            MarkerInfo info = resolveMarker(*freq_axis, display_dBm);
            drawMarkerOnPlot(info);
        }

        ImPlot::EndPlot();
    }

    double avg_noise = m_engine.computeAverageNoiseLevel(specs);
    ImGui::Text("Average noise level: %.2f dBm", avg_noise);

    drawMarkerControls(*freq_axis, display_dBm);

    ImGui::End();
}
