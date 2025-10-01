#include <cmath>
#include "spectrum-analyzer.h"
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
			std::vector<double> total(m_current_spectrum.signal.size());

			for (size_t i = 0; i < total.size(); ++i) {
				double signal_power_watts = m_current_spectrum.signal[i];
				double noise_power_watts = m_current_spectrum.noise[i];
				double total_power_watts = signal_power_watts + noise_power_watts;
				total[i] = 10.0 * std::log10(total_power_watts) + 30;
			}

			ImPlot::SetupAxes("Frequency (Hz)", "Magnitude");
			ImPlot::SetupAxisLimits(ImAxis_X1, MIN_FREQ, MAX_FREQ);
			ImPlot::SetupAxisLimits(ImAxis_Y1, -120, 10);
			ImPlot::PlotLine("Noisy Spectrum", m_current_spectrum.frequencies.data(), total.data(), m_current_spectrum.frequencies.size());
			ImPlot::EndPlot();
		}
		ImGui::End();
	}
}