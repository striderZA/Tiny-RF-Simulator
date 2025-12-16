#include "signal_generator_widget.h"
#include "common.h"
#include "imgui.h"
#include "logging_core.h"
#include "utils.h"

SignalGeneratorWidget::SignalGeneratorWidget(SignalGeneratorEngine &engine) : m_engine(engine) {}

void SignalGeneratorWidget::draw(const char *title, bool *p_open) {
    if (ImGui::Begin(title, p_open)) {
        double tone_frequency = m_engine.activeTone().first;
        double amplitude = m_engine.activeTone().second;
        double bin_width = m_engine.f_step_Hz();

        if (ImGui::Checkbox("Measure", &m_engine.node().view_enabled)) {
            LOG_INFO("Change measurement active state [gen%d -> %s].", m_engine.id(),
                     m_engine.node().view_enabled ? "True" : "False");
        }

        if (utils::inputDouble("Frequency (Hz)", tone_frequency, 1e6, 100e6, "%0.f", MIN_FREQ,
                               MAX_FREQ)) {
            m_engine.setToneFrequency(tone_frequency);
            LOG_INFO("Update tone frequency: [gen%d -> %.0f MHz].", m_engine.id(),
                     tone_frequency / 1e6);
        }

        if (utils::inputDouble("Amplitude (dBm)", amplitude, 1, 5, "%.0f", MIN_POWER, MAX_POWER)) {
            m_engine.setToneAmplitude(amplitude);
            LOG_INFO("Update tone amplitude: [gen%d -> %.0f dBm]", m_engine.id(), amplitude);
        }

        if (utils::inputDouble("Bin width (Hz)", bin_width, 1e6, 10e6, "%0.f", 1e6, 100e6)) {
            m_engine.setFreqStep(bin_width);
            LOG_INFO("Update bin width: [gen%d -> %.0f MHz] (noise floor: %.2f dBm)", m_engine.id(),
                     bin_width / 1e6, -174 + 10 * std::log10(bin_width));
        }

        ImGui::End();
    }
}
