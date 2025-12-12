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

    if (utils::inputDouble("Start Frequency (Hz)", s, 1e6, 100e6, "%0.f", MIN_FREQ, MAX_FREQ)) {
        m_engine.setStartFrequency(s);
        LOG_INFO("Update start frequency: %.0f Hz", m_engine.startFrequency());
    }

    if (utils::inputDouble("Stop Frequency (Hz)", e, 1e6, 100e6, "%0.f", MIN_FREQ, MAX_FREQ)) {
        m_engine.setStopFrequency(e);
        LOG_INFO("Update stop frequency: %.0f Hz", m_engine.stopFrequency());
    }

    if (utils::inputDouble("VBW (Hz)", vbw, 1e6, 10e6, "%.0f", 1e6, 100e6)) {
        m_engine.setVideoBw(vbw);
        LOG_INFO("Update VBW: %.0f Hz", m_engine.vbw());
    }

    if (utils::inputDouble("RBW (Hz)", rbw, 1e6, 10e6, "%.0f", 1e6, 100e6)) {
        m_engine.setResBw(rbw);
        LOG_INFO("Update RBW: %.0f Hz", m_engine.rbw());
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

    ImPlot::SetNextAxesLimits(m_engine.startFrequency(), m_engine.stopFrequency(),
                              m_engine.minPower(), m_engine.maxPower(), ImPlotCond_Always);

    if (ImPlot::BeginPlot("Spectrum")) {
        // Plot each active node's output as a separate line
        for (size_t i = 0; i < active_nodes.size(); ++i) {
            SignalNode *node = active_nodes[i];
            if (!node) {
                continue;
            }

            const Spectrum &spec = node->output;
            std::vector<double> display_dBm = m_engine.renderSpectrum(spec);

            const auto &freq = spec.frequencies;
            if (!freq.empty() && freq.size() == display_dBm.size()) {
                std::string label = std::string("Spectrum ") + std::to_string(i);
                ImPlot::PlotLine(label.c_str(), freq.data(), display_dBm.data(), (int)freq.size());
            }
        }
        ImPlot::EndPlot();
    }
    ImGui::End();
}
