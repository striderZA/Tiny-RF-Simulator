#include "spectrum_analyzer_widget.h"
#include "common.h"
#include "imgui.h"
#include "implot.h"
#include "logging_core.h"
#include "utils.h"
#include "view_manager.h"

SpectrumAnalyzerWidget::SpectrumAnalyzerWidget(SpectrumAnalyzerEngine &engine, ViewManager &vm)
    : m_engine(engine), m_view_manager(vm) {}

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
        ImPlot::EndPlot();
    }
    ImGui::End();
}
