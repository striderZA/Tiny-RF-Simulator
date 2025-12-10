#include "app.h"
#include "imgui.h"
#include "logging_widget.h"
#include <string>

RfSimulatorApp::RfSimulatorApp() {
    m_generators.push_back(std::make_unique<SignalGeneratorEngine>(0));
    m_generator_widgets.push_back(std::make_unique<SignalGeneratorWidget>(*m_generators.back()));
    m_generators.push_back(std::make_unique<SignalGeneratorEngine>(1));
    m_generator_widgets.push_back(std::make_unique<SignalGeneratorWidget>(*m_generators.back()));

    m_view_manager.registerNode(&m_generators[0]->node());
    m_view_manager.registerNode(&m_generators[1]->node());

    m_spectrum_widget = std::make_unique<SpectrumAnalyzerWidget>(m_spectrum_engine, m_view_manager);
}

void RfSimulatorApp::update_dsp() {

    for (auto &gen : m_generators) {
    }
}

void RfSimulatorApp::draw_ui() {
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate,
                io.Framerate);

    m_spectrum_widget->draw("Spectrum Analyzer");

    for (size_t i = 0; i < m_generator_widgets.size(); ++i) {
        std::string title = "Generator " + std::to_string(m_generators[i]->id());
        m_generator_widgets[i]->draw(title.c_str());
    }

    if (m_show_log)
        m_log_widget.draw("Log", &m_show_log);
}
