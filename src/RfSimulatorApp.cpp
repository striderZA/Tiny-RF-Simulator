#include "RfSimulatorApp.h"
#include "imgui.h"
#include <implot.h>
#include <cmath>
#include <algorithm>

RfSimulatorApp::RfSimulatorApp() : m_spectrum_analyzer(), m_signal_generator(0) {
	LOG_INFO("Application initialized!");
}

void RfSimulatorApp::onGui() {
	if (m_show_log) {
		ShowAppLog(&m_show_log);
	}

	ImGuiIO& io = ImGui::GetIO(); (void)io;
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

	m_spectrum_analyzer.update("Spectrum", nullptr);
	m_signal_generator.setupTone("Signal Generator", nullptr);
}