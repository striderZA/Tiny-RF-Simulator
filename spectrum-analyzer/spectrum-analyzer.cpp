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
		if (ImPlot::BeginPlot("Spectrum Analyzer")) {
			std::vector<double> total(m_current_spectrum.signal.size());

			for (size_t i = 0; i < total.size(); ++i) {
				total[i] = m_current_spectrum.signal[i] + m_current_spectrum.noise[i];
			}

			ImPlot::SetupAxes("Frequency (Hz)", "Magnitude");
			ImPlot::SetupAxisLimits(ImAxis_X1, -100, 100);
			ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, std::max(1.0, *std::max_element(m_current_spectrum.signal.begin(), m_current_spectrum.signal.end()) * 1.5));		ImPlot::PlotLine("Spectrum", m_current_spectrum.frequencies.data(), total.data(), m_current_spectrum.frequencies.size());
			ImPlot::PlotLine("Noisy Spectrum", m_current_spectrum.frequencies.data(), total.data(), m_current_spectrum.frequencies.size());
			ImPlot::EndPlot();
		}
		ImGui::End();
	}
}