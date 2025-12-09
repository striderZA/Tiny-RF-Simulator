#include "app.h"
#include "imgui.h"
#include <string>

RfSimulatorApp::RfSimulatorApp() {
	m_generators.push_back(std::make_unique<SignalGeneratorEngine>(0));
	m_generator_widgets.push_back(
		std::make_unique<SignalGeneratorWidget>(*m_generators.back())
	);
	m_generators.push_back(std::make_unique<SignalGeneratorEngine>(1));
	m_generator_widgets.push_back(
		std::make_unique<SignalGeneratorWidget>(*m_generators.back())
	);
	m_spectrum_widget = std::make_unique<SpectrumAnalyzerWidget>(m_spectrum_engine);
}

void RfSimulatorApp::update_dsp() {
	m_spectrum_engine.clearTones();

	// Add tone(s) from each generator
	for (auto& gen : m_generators) {
		m_spectrum_engine.addTone(gen->activeTone());
	}

	m_spectrum_engine.updateNoiseLevel();
	m_spectrum_engine.updateSpectrum();
}

void RfSimulatorApp::draw_ui() {
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
	m_spectrum_widget->draw("Spectrum Analyzer");

	for (size_t i = 0; i < m_generator_widgets.size(); ++i) {
		std::string title = "Generator " + std::to_string(m_generators[i]->id());
		m_generator_widgets[i]->draw(title.c_str());
	}
}