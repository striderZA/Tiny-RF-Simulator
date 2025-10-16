#include "RfSimulatorApp.h"
#include "imgui.h"
#include <implot.h>
#include <cmath>
#include <algorithm>

RfSimulatorApp::RfSimulatorApp() : m_spectrum_analyzer(), m_signal_generators() {
	LOG_INFO("Application initialized!");
	m_signal_generators.push_back(std::make_unique<SignalGenerator>(0));
}

void RfSimulatorApp::onGui() {
	if (m_show_log) {
		ShowAppLog(&m_show_log);
	}

	ImGuiIO& io = ImGui::GetIO(); (void)io;
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

	for (auto& gen : m_signal_generators) {
		gen->setupTone("SigGen", nullptr);
	}

	m_spectrum_analyzer.update("Spectrum", nullptr);

	this->drawSignalGenerators("Signal Generators", nullptr);
}

void RfSimulatorApp::drawSignalGenerators(const char* title, bool* p_open) {
}