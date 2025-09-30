#include "rf-simulator-app.h"
#include "imgui.h"
#include <implot.h>
RfSimulatorApp::RfSimulatorApp() : m_siggen() {
	Spectrum dummy_input;
	m_siggen.process(dummy_input, m_current_spectrum);
}

void RfSimulatorApp::onGui() {
	static bool showDemo = true;
	if (showDemo == true) {
		ImGui::ShowDemoWindow(&showDemo);
	}
	

	static bool showImPlotDemo = true;
	if (showImPlotDemo == true){
		ImPlot::ShowDemoWindow(&showImPlotDemo);
	}
SignalGenerator::SignalGenerator(float freq, float amp, float noise_floor) : m_freq(freq), m_amp(amp), m_noise_floor(noise_floor) {}

void SignalGenerator::process(const Spectrum& input, Spectrum& output)
{
	output.frequencies.resize(num_bins);
	double bin_bw = (max_freq - min_freq) / (num_bins - 1);
	for (size_t i = 0; i < num_bins; ++i) {
		output.frequencies[i] = min_freq + i * bin_bw;
	}

	output.signal.resize(num_bins, 0.0);
	size_t closest_bin = static_cast<size_t>(std::round((m_freq - min_freq) / bin_bw));
	if (closest_bin < num_bins) {
		output.signal[closest_bin] = m_amp;
	}

	output.noise.resize(num_bins, m_noise_floor);
}