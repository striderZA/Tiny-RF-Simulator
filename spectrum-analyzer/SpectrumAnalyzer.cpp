#include <cmath>
#include "SpectrumAnalyzer.h"
#include "imgui.h"

SpectrumAnalyzer::SpectrumAnalyzer() {}

void SpectrumAnalyzer::analyze(const Spectrum& input) {
	m_current_spectrum = input;
	m_show_spectrum = true;
}

void SpectrumAnalyzer::draw(const char* title, bool* p_open) {
	if (!m_show_spectrum || m_current_spectrum.frequencies.empty()) {
		return;
	}

	if (ImGui::Begin(title, p_open)) {
		// Frequency range inputs with validation
		ImGui::InputDouble("Start Frequency (Hz)", &m_start_freq, 1.0, 10.0, "%.0f");
		ImGui::InputDouble("Stop Frequency (Hz)", &m_stop_freq, 1.0, 10.0, "%.0f");

		if (m_start_freq < MIN_FREQ) m_start_freq = MIN_FREQ;
		if (m_stop_freq > MAX_FREQ) m_stop_freq = MAX_FREQ;
		if (m_start_freq >= m_stop_freq) {
			double temp = m_start_freq;
			m_start_freq = m_stop_freq - 1.0;
			m_stop_freq = temp + 1.0;
		}

		if (ImPlot::BeginPlot("Spectrum Analyzer")) {
			// Compute total power in dBm, filtered by frequency range if needed
			std::vector<double> total;
			std::vector<double> freq_subset;
			for (size_t i = 0; i < m_current_spectrum.frequencies.size(); ++i) {
				if (m_current_spectrum.frequencies[i] >= m_start_freq && m_current_spectrum.frequencies[i] <= m_stop_freq) {
					double signal_power_watts = m_current_spectrum.signal[i];
					double noise_power_watts = m_current_spectrum.noise[i];
					double total_power_watts = signal_power_watts + noise_power_watts;
					total.push_back(10.0 * std::log10(total_power_watts) + 30);  // Correct dBm conversion
					freq_subset.push_back(m_current_spectrum.frequencies[i]);
				}
			}

			ImPlot::SetupAxes("Frequency (Hz)", "Power (dBm)");
			ImPlot::SetupAxisLimits(ImAxis_X1, m_start_freq, m_stop_freq);
			ImPlot::SetupAxisLimits(ImAxis_Y1, m_min_power, m_max_power);

			if (!total.empty()) {
				ImPlot::PlotLine("Noisy Spectrum", freq_subset.data(), total.data(), total.size());
			}

			ImPlot::EndPlot();
		}
		ImGui::End();
	}
void SpectrumAnalyzer::updateSpectrum() {
	int num_points = std::round((m_stop_freq - m_start_freq) / m_vbw);
	m_current_spectrum.frequencies = std::vector<double>(num_points, 0);
	m_current_spectrum.noise_power_W = std::vector<double>(num_points, 0);
	m_current_spectrum.signal_power_W = std::vector<double>(num_points, 0);

	for (int i = 0; i < num_points; ++i) {
		m_current_spectrum.frequencies[i] = (m_start_freq + i * m_vbw);
	}
	m_current_spectrum.noise_power_W = this->generateNoiseVector();
}

void SpectrumAnalyzer::updateNoiseLevel() {
	m_noise_level_dBm = MIN_POWER + 10 * std::log10(m_rbw);
}