#include "RfSimulatorApp.h"
#include "imgui.h"
#include <implot.h>
#include <cmath>
#include <algorithm>

RfSimulatorApp::RfSimulatorApp() : m_spectrum_analyzer(), m_signal_generators() {
	LOG_INFO("Application initialized!");
	m_signal_generators.push_back(std::make_unique<SignalGenerator>(static_cast<int>(InputSignals::G0)));
	m_signal_generators.push_back(std::make_unique<SignalGenerator>(static_cast<int>(InputSignals::G1)));
}

void RfSimulatorApp::onGui() {
	if (m_show_log) {
		ShowAppLog(&m_show_log);
	}

	ImGuiIO& io = ImGui::GetIO(); (void)io;
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

	m_spectrum_analyzer.update("Spectrum", nullptr);

	this->drawSignalGenerators("Signal Generators", nullptr);

	for (auto& gen : m_signal_generators) {
		std::string title = "Generator " + std::to_string(gen->id());
		gen->setup(title.c_str(), nullptr);
	}
}

void RfSimulatorApp::drawSignalGenerators(const char* title, bool* p_open) {
}