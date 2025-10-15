#include <cmath>
#include "SpectrumAnalyzer.h"
#include "imgui.h"

SpectrumAnalyzer::SpectrumAnalyzer() {
	m_active_tone = std::make_pair<int, double>(1, -20);
}

void SpectrumAnalyzer::analyze(const Spectrum& input) {
	m_current_spectrum = input;
	m_show_spectrum = true;
}

static std::vector<double> to_dBm(std::vector<double> input) {
	std::vector<double> output = std::vector<double>(input.size());
	for (int i = 0; i < output.size(); ++i) {
		output[i] = 10 * std::log10(input[i]) + 30;
	}
	return output;
}

static std::vector<double> to_W(std::vector<double> input) {
	std::vector<double> output = std::vector<double>(input.size());
	for (int i = 0; i < output.size(); ++i) {
		output[i] = std::pow(10, (input[i]-30)/10)
	}
	return output;
}

double SpectrumAnalyzer::generateNoiseSample() const {
	std::normal_distribution<double> dist(0.0, 1.0);
	std::random_device rd;
	std::mt19937 generator(rd());
	double noise = dist(generator);
	double rms_voltage = std::sqrt(4 * k * T * m_rbw * R);

	return std::pow(noise * rms_voltage, 2) / R;
}

std::vector<double> SpectrumAnalyzer::generateNoiseVector() {
	size_t length = m_current_spectrum.frequencies.size();
	std::vector<double> noiseSamples(length);
	for (int i = 0; i < length; ++i) {
		noiseSamples[i] = this->generateNoiseSample();
	}
	return noiseSamples;
}

void SpectrumAnalyzer::update(const char* title, bool* p_open) {
	if (ImGui::Begin(title, p_open)) {
		ImGui::InputDouble("Start Frequency (Hz)", &m_start_freq, 1e6, 100e6, "%.0f");
		ImGui::InputDouble("Stop Frequency (Hz)", &m_stop_freq, 1e6, 100e6, "%.0f");
		ImGui::InputDouble("VBW (Hz)", &m_vbw, 1e6, 10e6, "%.0f");
		ImGui::InputDouble("RBW (Hz)", &m_rbw, 1e6, 10e6, "%.0f");
		ImGui::InputDouble("Ref (dBm)", &m_max_power, 5, 10, "%.0f");
		ImGui::InputDouble("Min level (dBm)", &m_min_power, 5, 10, "%.0f");
		ImGui::Text("Noise: %.2f dBm", m_noise_level_dBm);
		ImPlot::SetNextAxesLimits(m_start_freq, m_stop_freq, m_min_power, m_max_power, 1);

		if (ImPlot::BeginPlot("Spectrum Analyzer")) {
			this->updateNoiseLevel();
			this->updateSpectrum();
			ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.0f);
			ImPlot::PlotLine("Spectrum", m_current_spectrum.frequencies.data(), to_dBm(m_current_spectrum.noise_power_W).data(), m_current_spectrum.frequencies.size());
			ImPlot::PopStyleVar();
			ImPlot::EndPlot();
		}
		ImGui::End();
	}
}

void SpectrumAnalyzer::updateSpectrum() {
	int num_points = std::round((m_stop_freq - m_start_freq) / m_vbw);
	m_current_spectrum.frequencies = std::vector<double>(num_points, 0);
	m_current_spectrum.noise_power_W = std::vector<double>(num_points, 0);

	for (int i = 0; i < num_points; ++i) {
		m_current_spectrum.frequencies[i] = (m_start_freq + i * m_vbw);
	}
	m_current_spectrum.noise_power_W = this->generateNoiseVector();
}

void SpectrumAnalyzer::updateNoiseLevel() {
	m_noise_level_dBm = MIN_POWER + 10 * std::log10(m_rbw);
}