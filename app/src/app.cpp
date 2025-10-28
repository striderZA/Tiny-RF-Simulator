#include "app.h"
#include "imgui.h"
#include <string>
#include <signal_generator.h>
#include <memory>
#include <imnodes.h>
#include "logging.h"

RfSimulatorApp::RfSimulatorApp()
	: m_spectrum_analyzer(), m_signal_generators(), m_enable_log(true), m_node_editor() {
	m_signal_generators.push_back(
		std::make_unique<SignalGenerator>(static_cast<int>(InputSignals::G0)));
	m_signal_generators.push_back(
		std::make_unique<SignalGenerator>(static_cast<int>(InputSignals::G1)));
	m_signal_generators.push_back(
		std::make_unique<SignalGenerator>(static_cast<int>(InputSignals::G2)));
	m_signal_generators.push_back(
		std::make_unique<SignalGenerator>(static_cast<int>(InputSignals::G3)));

	m_node_editor.initialize();
}

void RfSimulatorApp::onGui() {
	ImGui::Begin("Debug");
	ImGui::Checkbox("Enable log", &m_enable_log);
	if (m_enable_log) {
		ShowAppLog();
	}

	ImGuiIO& io = ImGui::GetIO();
	(void)io;
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
		1000.0f / io.Framerate, io.Framerate);
	ImGui::End();

	for (auto& gen : m_signal_generators) {
		std::string title = "Generator " + std::to_string(gen->id());
		gen->setup(title.c_str(), nullptr);
		if (gen->measurementActive()) {
			m_spectrum_analyzer.addToneRef(&gen->m_active_tone);
		}
		else {
			m_spectrum_analyzer.removeToneRef(&gen->m_active_tone);
		}
	}

	m_spectrum_analyzer.draw("Spectrum", nullptr);

	ImGui::Begin("node editor");
	m_node_editor.draw();
	ImGui::End();
}