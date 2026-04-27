#include "app.h"
#include "imgui.h"
#include "logging_widget.h"
#include <algorithm>

RfSimulatorApp::RfSimulatorApp() {
    static const int defaultGeneratorCount = static_cast<int>(InputSignals::COUNT);
    static const int defaultAmplifierCount = 1;
    for (int i = 0; i < defaultGeneratorCount; ++i) {
        addGenerator();
    }
    for (int i = 0; i < defaultAmplifierCount; ++i) {
        addAmplifier();
    }
    m_spectrum_widget = std::make_unique<SpectrumAnalyzerWidget>(m_spectrum_engine, m_view_manager);
}

void RfSimulatorApp::update_dsp() {

    for (auto &gen : m_generators) {
        gen->update(0.0);
    }

    size_t count = std::min(m_generators.size(), m_amplifiers.size());
    for (size_t i = 0; i < count; ++i) {
        m_amplifiers[i]->node().input = m_generators[i]->node().output;
        m_amplifiers[i]->update(0.0);
    }
}

void RfSimulatorApp::addGenerator() {
    int id = static_cast<int>(m_generators.size());
    m_generators.push_back(std::make_unique<SignalGeneratorEngine>(id));
    m_generator_widgets.push_back(std::make_unique<SignalGeneratorWidget>(*m_generators.back()));
    m_view_manager.registerNode(&m_generators.back()->node());
}

void RfSimulatorApp::removeGenerator(size_t index) {
    if (index >= m_generators.size()) return;
    m_view_manager.unregisterNode(&m_generators[index]->node());
    m_generators.erase(m_generators.begin() + index);
    m_generator_widgets.erase(m_generator_widgets.begin() + index);
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
    m_amplifiers.erase(m_amplifiers.begin() + index);
    m_amplifier_widgets.erase(m_amplifier_widgets.begin() + index);
}

void RfSimulatorApp::draw_ui() {
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate,
                io.Framerate);

    draw_signal_chain("Signal Chain");

    m_spectrum_widget->draw("Spectrum Analyzer");

    for (size_t i = 0; i < m_generator_widgets.size(); ++i) {
        char title_buffer[64];
        std::snprintf(title_buffer, sizeof(title_buffer), "Generator %d##gen%zu",
                      m_generators[i]->id(), i);
        m_generator_widgets[i]->draw(title_buffer);
    }

    for (size_t i = 0; i < m_amplifier_widgets.size(); ++i) {
        char title_buffer[64];
        std::snprintf(title_buffer, sizeof(title_buffer), "Amplifier %d##amp%zu",
                      m_amplifiers[i]->id(), i);
        m_amplifier_widgets[i]->draw(title_buffer);
    }

    if (m_show_log)
        m_log_widget.draw("Log", &m_show_log);
}

void RfSimulatorApp::draw_signal_chain(const char* title) {
    if (ImGui::Begin(title)) {
        ImGui::Text("Generators: %zu", m_generators.size());
        ImGui::SameLine();
        if (ImGui::Button("Add Generator")) {
            addGenerator();
        }
        for (size_t i = 0; i < m_generators.size(); ++i) {
            ImGui::PushID(i);
            ImGui::Text("Generator %d", m_generators[i]->id());
            ImGui::SameLine();
            if (ImGui::Button("Remove")) {
                removeGenerator(i);
                ImGui::PopID();
                break; // vector invalidated
            }
            ImGui::PopID();
        }
        ImGui::Separator();
        ImGui::Text("Amplifiers: %zu", m_amplifiers.size());
        ImGui::SameLine();
        if (ImGui::Button("Add Amplifier")) {
            addAmplifier();
        }
        for (size_t i = 0; i < m_amplifiers.size(); ++i) {
            ImGui::PushID(i + 1000);
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
