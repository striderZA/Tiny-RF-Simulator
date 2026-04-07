#include "amplifier_widget.h"
#include "imgui.h"
#include "logging_core.h"
#include "utils.h"

AmplifierWidget::AmplifierWidget(AmplifierEngine &engine) : m_engine(engine) {}

void AmplifierWidget::draw(const char *title, bool *p_open) {
    if (ImGui::Begin(title, p_open)) {

        if (ImGui::Checkbox("Measure", &m_engine.node().view_enabled)) {
            LOG_INFO("Change measurement active state [amp%d -> %s].", m_engine.id(),
                     m_engine.node().view_enabled ? "True" : "False");
        }
        double gain_dB = m_engine.gain_dB();
        double nf_dB = m_engine.nf_dB();
        double bin_width_Hz = m_engine.f_step_Hz();

        if (utils::inputDouble("Gain (dB)", gain_dB, 1, 10, "%.1f", -10, 40)) {
            m_engine.setGain_dB(gain_dB);
            LOG_INFO("Update amplifier gain: [amp%d -> %.1f dB]", m_engine.id(), gain_dB);
        }

        if (utils::inputDouble("Noise Figure (dB)", nf_dB, 0.1, 10, "%.1f", 0.0, 30.0)) {
            m_engine.setNF_dB(nf_dB);
            LOG_INFO("Update amplifier NF: [amp%d -> %.1f dB]", m_engine.id(), nf_dB);
        }

        if (utils::inputFrequency("Bin width (MHz)", bin_width_Hz, 1.0, 10.0, "%.0f", 1e6, 100e6)) {
            m_engine.setFreqStep(bin_width_Hz);
            LOG_INFO("Update amplifier bin width: [amp%d -> %.0f MHz]", m_engine.id(),
                     bin_width_Hz / 1e6);
        }

        ImGui::End();
    }
}
