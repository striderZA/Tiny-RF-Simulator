#include "rf-simulator-app.h"
#include "imgui.h"
#include <implot.h>
#include <cmath>
#include <algorithm>

RfSimulatorApp::RfSimulatorApp() : m_signal_generators() {
	for (int i = 0; i < NUM_GENERATORS; ++i) {
		m_signal_generators.emplace_back(i);
	}

	LOG_INFO("Application initialized!");
}

void RfSimulatorApp::onGui() {
	if (m_show_log) {
		ShowAppLog(&m_show_log);
	}

	static bool showDemo = false;
	ImGui::Begin("ImGui Demo");
	ImGui::Checkbox("Show ImGui Demo", &showDemo);
	if (showDemo == true) {
		ImGui::ShowDemoWindow(&showDemo);
	}
	ImGui::End();

	static bool showImPlotDemo = false;
	ImGui::Begin("ImPlot Demo");
	ImGui::Checkbox("Show ImPlot Demo", &showImPlotDemo);
	if (showImPlotDemo == true) {
		ImPlot::ShowDemoWindow(&showImPlotDemo);
	}
	ImGui::End();

	// Signal Generator Window
	ImGui::Begin("Signal Generator");

	for (int i = 0; i < NUM_GENERATORS; ++i) {
		auto& gen = m_signal_generators[i];
		char label[32];
		snprintf(label, sizeof(label), "Generator %d", i);
		if (ImGui::CollapsingHeader(label)) {
			if (i == m_selected_port) {
				float freq = gen.freq();
				float amp = gen.amp();
				float noise = gen.noise();

				if (ImGui::InputFloat("Frequency", &freq)) {
					gen.setFrequency(freq);
					Spectrum output;
					gen.process(Spectrum(), output);
					m_spectrum_analyzer.analyze(output);
					LOG_INFO("Frequency updated to %.2f Hz for Generator %d", freq, i);
				}

				if (ImGui::InputFloat("Amplitude", &amp)) {
					gen.setAmplitude(amp);
					Spectrum output;
					gen.process(Spectrum(), output);
					m_spectrum_analyzer.analyze(output);
					LOG_INFO("Amplitude updated to %.2f V for Generator %d", amp, i);
				}

				if (ImGui::InputFloat("Noise", &noise)) {
					gen.setNoiseFloor(noise);
					Spectrum output;
					gen.process(Spectrum(), output);
					m_spectrum_analyzer.analyze(output);
					LOG_INFO("Noise updated to %.2f for Generator %d", noise, i);
				}
			}
			else {
				ImGui::Text("Frequency: %.2f Hz", gen.freq());
				ImGui::Text("Amplitude: %.2f V", gen.amp());
				ImGui::Text("Noise floor: %.2f Hz", gen.noise());
			}
		}
	}

	ImGui::Separator();
	const char* port_labels[] = { "Generator 0" };
	ImGui::Combo("Analyze Port", &m_selected_port, port_labels, NUM_GENERATORS);
	if (ImGui::Button("Update and Analyze Spectrum")) {
		Spectrum output;
		m_signal_generators[m_selected_port].process(Spectrum(), output);  // Process selected generator
		m_spectrum_analyzer.analyze(output);
		LOG_INFO("Spectrum analyzed for port %d", m_selected_port);
	}
	ImGui::End();

	m_spectrum_analyzer.draw("Spectrum analyzer", nullptr);
}