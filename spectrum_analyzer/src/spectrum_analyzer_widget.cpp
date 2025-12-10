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

    m_engine.updateNoiseLevel();
    m_engine.updateSpectrum();


    const auto &spec = m_engine.spectrum();
    const auto dBm = to_dBm(spec.noise_power_W);

    ImPlot::SetNextAxesLimits(m_engine.startFrequency(), m_engine.stopFrequency(),
                              m_engine.minPower(), m_engine.maxPower(), ImPlotCond_Always);

    if (ImPlot::BeginPlot("Spectrum Analyzer")) {
        ImPlot::PlotLine("Spectrum", spec.frequencies.data(), dBm.data(),
                         (int)spec.frequencies.size());
        ImPlot::EndPlot();
    }

    ImGui::End();
}
