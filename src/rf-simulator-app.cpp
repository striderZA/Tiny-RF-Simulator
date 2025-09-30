#include "rf-simulator-app.h"
#include "imgui.h"
#include <implot.h>
#include <cmath>
#include <algorithm>

RfSimulatorApp::RfSimulatorApp() : m_siggen() {
	Spectrum dummy_input;
	m_siggen.process(dummy_input, m_current_spectrum);
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

	float frequency = m_siggen.m_freq;  // Default frequency in Hz (or arbitrary units)
	float amplitude = m_siggen.m_amp;  // Default amplitude (arbitrary units)
	float noise = m_siggen.m_noise_floor;
	static bool show_spectrum = true;

	// Signal Generator Window
	ImGui::Begin("Signal Generator");

	if (ImGui::InputFloat("Frequency", &frequency)) {
		m_siggen.setFrequency(frequency);
	}

	if (ImGui::InputFloat("Amplitude", &amplitude)) {
		m_siggen.setAmplitude(amplitude);
	}

	if (ImGui::InputFloat("Noise Floor", &noise)) {
		m_siggen.setNoiseFloor(noise);
	}

	ImGui::Checkbox("Show Spectrum", &show_spectrum);

	if (show_spectrum) {
		Spectrum dummy_input;
		m_siggen.process(dummy_input, m_current_spectrum);
		ImGui::Begin("Output Spectrum", &show_spectrum);

		if (ImPlot::BeginPlot("Spectrum Plot")) {
			std::vector<double> combined_spectrum(m_current_spectrum.signal.size());
			for (size_t i = 0; i < combined_spectrum.size(); ++i) {
				combined_spectrum[i] = m_current_spectrum.signal[i] + m_current_spectrum.noise[i];
			}
			ImPlot::SetupAxes("Frequency (Hz)", "Magnitude");
			ImPlot::SetupAxisLimits(ImAxis_X1, SignalGenerator::min_freq, SignalGenerator::max_freq);
			ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, std::max(1.0, m_siggen.m_amp * 1.5));
			ImPlot::PlotLine("Spectrum", m_current_spectrum.frequencies.data(), combined_spectrum.data(), m_current_spectrum.frequencies.size());
			ImPlot::EndPlot();
		}

		ImGui::End();
	}

	ImGui::End();
}

SignalGenerator::SignalGenerator(float freq, float amp, float noise_floor) : m_freq(freq), m_amp(amp), m_noise_floor(noise_floor) {}

void SignalGenerator::process(const Spectrum& input, Spectrum& output)
{
	output.frequencies.resize(num_bins);
	double bin_bw = (max_freq - min_freq) / (num_bins - 1);
	for (size_t i = 0; i < num_bins; ++i) {
		output.frequencies[i] = min_freq + i * bin_bw;
	}

	output.signal.assign(num_bins, 0.0);
	output.noise.assign(num_bins, m_noise_floor);

	output.signal.resize(num_bins, 0.0);
	size_t closest_bin = static_cast<size_t>(std::round((m_freq - min_freq) / bin_bw));
	if (closest_bin < num_bins) {
		output.signal[closest_bin] = m_amp;
	}

	output.noise.resize(num_bins, m_noise_floor);
}