#include "signal_generator_widget.h"
#include "common.h"
#include "imgui.h"
#include "logging_core.h"
#include "utils.h"
#include <algorithm>
#include <string>

SignalGeneratorWidget::SignalGeneratorWidget(SignalGeneratorEngine &engine)
    : m_engine(engine), m_selectedTone(0) {}

void SignalGeneratorWidget::draw(const char *title, bool *p_open) {
    if (ImGui::Begin(title, p_open)) {
        if (ImGui::Checkbox("Measure", &m_engine.node().view_enabled)) {
            LOG_INFO("Change measurement active state [gen%d -> %s].", m_engine.id(),
                     m_engine.node().view_enabled ? "True" : "False");
        }

        int tone_count = static_cast<int>(m_engine.toneCount());
        m_selectedTone = std::clamp(m_selectedTone, 0, std::max(0, tone_count - 1));

        // Tone list header
        ImGui::SeparatorText("Tones");

        if (tone_count > 0) {
            // Build a list of tone labels
            std::vector<std::string> labels;
            labels.reserve(static_cast<size_t>(tone_count));
            const auto &tone_vec = m_engine.tones();
            for (int i = 0; i < tone_count; ++i) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "Tone %d: %.0f MHz @ %.0f dBm", i,
                              tone_vec[static_cast<size_t>(i)].freq_Hz / 1e6,
                              tone_vec[static_cast<size_t>(i)].power_dBm);
                labels.emplace_back(buf);
            }

            // List box to select tone
            std::vector<const char *> cstrings;
            cstrings.reserve(labels.size());
            for (const auto &s : labels) {
                cstrings.push_back(s.c_str());
            }
            ImGui::ListBox("##tone_list", &m_selectedTone, cstrings.data(),
                           static_cast<int>(cstrings.size()), 4);

            // Edit selected tone
            auto &tone = tone_vec[static_cast<size_t>(m_selectedTone)];
            double freq_Hz = tone.freq_Hz;
            double power_dBm = tone.power_dBm;

            if (utils::inputFrequency("Frequency (MHz)", freq_Hz, 1.0, 100.0, "%.0f", MIN_FREQ,
                                      MAX_FREQ)) {
                m_engine.updateTone(static_cast<size_t>(m_selectedTone), freq_Hz, power_dBm);
                LOG_INFO("Update tone %d frequency: [gen%d -> %.0f MHz].", m_selectedTone,
                         m_engine.id(), freq_Hz / 1e6);
            }

            if (utils::inputDouble("Amplitude (dBm)", power_dBm, 1, 5, "%.0f", MIN_POWER,
                                   MAX_POWER)) {
                m_engine.updateTone(static_cast<size_t>(m_selectedTone), freq_Hz, power_dBm);
                LOG_INFO("Update tone %d amplitude: [gen%d -> %.0f dBm]", m_selectedTone,
                         m_engine.id(), power_dBm);
            }

            if (ImGui::Button("Remove Selected Tone")) {
                m_engine.removeTone(static_cast<size_t>(m_selectedTone));
                LOG_INFO("Remove tone %d: [gen%d].", m_selectedTone, m_engine.id());
            }
        } else {
            ImGui::Text("No tones configured.");
        }

        if (ImGui::Button("Add Tone")) {
            m_engine.addTone(100e6, -20.0);
            m_selectedTone = static_cast<int>(m_engine.toneCount()) - 1;
            LOG_INFO("Add new tone: [gen%d].", m_engine.id());
        }

        ImGui::End();
    }
}
