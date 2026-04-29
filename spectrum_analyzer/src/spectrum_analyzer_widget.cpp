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
        if (m_marker.enabled) {
            m_marker.target_freq_Hz =
                (m_engine.startFrequency() + m_engine.stopFrequency()) / 2.0;
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
            int closest = 0;
            double best_dist =
                std::abs(m_marker.peaks[0].freq_Hz - m_marker.target_freq_Hz);
            for (size_t i = 1; i < m_marker.peaks.size(); ++i) {
                double d =
                    std::abs(m_marker.peaks[i].freq_Hz - m_marker.target_freq_Hz);
                if (d < best_dist) {
                    best_dist = d;
                    closest = static_cast<int>(i);
                }
            }
            int next = (closest + 1) % static_cast<int>(m_marker.peaks.size());
            m_marker.target_freq_Hz = m_marker.peaks[next].freq_Hz;
        }
    }

    double bin_step =
        (freq_axis.size() > 1) ? (freq_axis[1] - freq_axis[0]) : 1e6;
    if (ImGui::Button("<")) {
        m_marker.target_freq_Hz -= bin_step;
        if (m_marker.target_freq_Hz < freq_axis.front()) {
            m_marker.target_freq_Hz = freq_axis.front();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(">")) {
        m_marker.target_freq_Hz += bin_step;
        if (m_marker.target_freq_Hz > freq_axis.back()) {
            m_marker.target_freq_Hz = freq_axis.back();
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
            int idx = resolveMarkerIdx(*freq_axis, display_dBm);
            if (idx >= 0 && idx < static_cast<int>(display_dBm.size())) {
                drawMarkerOnPlot((*freq_axis)[idx], display_dBm[idx]);
            }
        }

        ImPlot::EndPlot();
    }

    double avg_noise = m_engine.computeAverageNoiseLevel(specs);
    ImGui::Text("Average noise level: %.2f dBm", avg_noise);

    drawMarkerControls(*freq_axis, display_dBm);

    ImGui::End();
}
