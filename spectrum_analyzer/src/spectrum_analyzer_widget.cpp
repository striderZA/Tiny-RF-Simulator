#include "spectrum_analyzer_widget.h"
#include "common.h"
#include "imgui.h"
#include "implot.h"
#include "utils.h"
#include "view_manager.h"

SpectrumAnalyzerWidget::SpectrumAnalyzerWidget(SpectrumAnalyzerEngine &engine, ViewManager &vm)
    : m_engine(engine), m_view_manager(vm) {}

void SpectrumAnalyzerWidget::draw(const char *title, bool *p_open) {
    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    // UI → Engine parameters
    double s = m_engine.startFrequency();
    double e = m_engine.stopFrequency();
    double vbw = m_engine.vbw();
    double rbw = m_engine.rbw();
    double minp = m_engine.minPower();
    double maxp = m_engine.maxPower();

    if (utils::inputDouble("Start Frequency (Hz)", s, 1e6, 100e6, "%0.f", MIN_FREQ, MAX_FREQ)) {
        m_engine.setStartFrequency(s);
    }

    if (utils::inputDouble("Stop Frequency (Hz)", e, 1e6, 100e6, "%0.f", MIN_FREQ, MAX_FREQ)) {
        m_engine.setStopFrequency(e);
    }

    if (utils::inputDouble("VBW (Hz)", vbw, 1e6, 10e6, "%.0f", 1e6, 100e6)) {
        m_engine.setVideoBw(vbw);
    }

    if (utils::inputDouble("RBW (Hz)", rbw, 1e6, 10e6, "%.0f", 1e6, 100e6)) {
        m_engine.setResBw(rbw);
    }

    if (utils::inputDouble("Ref (dBm)", maxp, 5, 10, "%.0f", MIN_POWER, MAX_POWER)) {
        m_engine.setMaxPower(maxp);
    }

    if (utils::inputDouble("Min level (dBm)", minp, 5, 10, "%.0f", MIN_POWER, MAX_POWER)) {
        m_engine.setMinPower(minp);
    }

    SignalNode *node = m_view_manager.getActiveNode();

    if (!node) {
        ImGui::Text("No signal node selected for display!");
        ImGui::End();
        return;
    }

    const Spectrum &spec = node->output;
    std::vector<double> display_dBm = m_engine.renderSpectrum(spec);

    ImPlot::SetNextAxesLimits(m_engine.startFrequency(), m_engine.stopFrequency(),
                              m_engine.minPower(), m_engine.maxPower(), ImPlotCond_Always);

    if (ImPlot::BeginPlot("Spectrum")) {
        const auto &freq = spec.frequencies;
        if (!freq.empty() && freq.size() == display_dBm.size()) {
            ImPlot::PlotLine("Spectrum", freq.data(), display_dBm.data(), (int)freq.size());
        }
        ImPlot::EndPlot();
    }
    ImGui::End();
}
