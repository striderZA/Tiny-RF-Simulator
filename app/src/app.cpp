#include "app.h"
#include "imgui.h"
#include "logging_widget.h"
#include <string>

RfSimulatorApp::RfSimulatorApp() {
    for (int i = static_cast<int>(InputSignals::G0); i < static_cast<int>(InputSignals::COUNT);
         ++i) {

        m_generators.push_back(std::make_unique<SignalGeneratorEngine>(i));
        m_generator_widgets.push_back(
            std::make_unique<SignalGeneratorWidget>(*m_generators.back()));

        m_view_manager.registerNode(&m_generators.back()->node());
    }
    m_spectrum_widget = std::make_unique<SpectrumAnalyzerWidget>(m_spectrum_engine, m_view_manager);
}

void RfSimulatorApp::update_dsp() {

    for (auto &gen : m_generators) {
        gen->update(0.0);
    }
}

void RfSimulatorApp::draw_ui() {
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate,
                io.Framerate);

    m_spectrum_widget->draw("Spectrum Analyzer");

    for (size_t i = 0; i < m_generator_widgets.size(); ++i) {
        char title_buffer[64];
        std::snprintf(title_buffer, sizeof(title_buffer), "Generator %d##gen%zu",
                      m_generators[i]->id(), i);
        m_generator_widgets[i]->draw(title_buffer);
    }

    if (m_show_log)
        m_log_widget.draw("Log", &m_show_log);
}
