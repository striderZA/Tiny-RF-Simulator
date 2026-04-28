#include "app.h"
#include "imgui.h"
#include "logging_widget.h"

RfSimulatorApp::RfSimulatorApp() {
    m_generator = std::make_unique<SignalGeneratorEngine>(0);
    m_generator_widget = std::make_unique<SignalGeneratorWidget>(*m_generator);
    m_generator->addTone(100e6, -20.0);
    m_view_manager.registerNode(&m_generator->node());

    static const int defaultAmplifierCount = 1;
    for (int i = 0; i < defaultAmplifierCount; ++i) {
        addAmplifier();
    }
    m_spectrum_widget = std::make_unique<SpectrumAnalyzerWidget>(m_spectrum_engine, m_view_manager);
}

void RfSimulatorApp::update_dsp() {
    m_generator->update(0.0);

    if (!m_amplifiers.empty()) {
        m_amplifiers[0]->node().input = m_generator->node().output;
        m_amplifiers[0]->update(0.0);
    }
}

void RfSimulatorApp::addAmplifier() {
    int id = static_cast<int>(m_amplifiers.size());
    m_amplifiers.push_back(std::make_unique<AmplifierEngine>(id));
    m_amplifier_widgets.push_back(std::make_unique<AmplifierWidget>(*m_amplifiers.back()));
    m_view_manager.registerNode(&m_amplifiers.back()->node());
}

void RfSimulatorApp::removeAmplifier(size_t index) {
    if (index >= m_amplifiers.size()) return;
    m_view_manager.unregisterNode(&m_amplifiers[index]->node());
    m_amplifiers.erase(m_amplifiers.begin() + static_cast<std::ptrdiff_t>(index));
    m_amplifier_widgets.erase(m_amplifier_widgets.begin() + static_cast<std::ptrdiff_t>(index));
}

void RfSimulatorApp::draw_ui() {
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate,
                io.Framerate);

    draw_signal_chain("Signal Chain");

    m_spectrum_widget->draw("Spectrum Analyzer");

    m_generator_widget->draw("Generator 0##gen0");

    for (size_t i = 0; i < m_amplifier_widgets.size(); ++i) {
        char title_buffer[64];
        std::snprintf(title_buffer, sizeof(title_buffer), "Amplifier %d##amp%zu",
                      m_amplifiers[i]->id(), i);
        m_amplifier_widgets[i]->draw(title_buffer);
    }

    if (m_show_log)
        m_log_widget.draw("Log", &m_show_log);
}

void RfSimulatorApp::draw_signal_chain(const char *title) {
    if (ImGui::Begin(title)) {
        ImGui::Text("Generator: 1");
        ImGui::Separator();
        ImGui::Text("Amplifiers: %zu", m_amplifiers.size());
        ImGui::SameLine();
        if (ImGui::Button("Add Amplifier")) {
            addAmplifier();
        }
        for (size_t i = 0; i < m_amplifiers.size(); ++i) {
            ImGui::PushID(static_cast<int>(i + 1000));
            ImGui::Text("Amplifier %d", m_amplifiers[i]->id());
            ImGui::SameLine();
            if (ImGui::Button("Remove")) {
                removeAmplifier(i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
}
