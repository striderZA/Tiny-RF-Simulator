#include <cmath>
#include "spectrum_analyzer.h"
#include "imgui.h"
#include "logging.h"
#include <common.h>
#include <implot.h>
#include <memory>
#include <random>
#include <vector>

SpectrumAnalyzer::SpectrumAnalyzer() : m_current_spectrum() {
	LOG_INFO("Spectrum analyzer setup complete!");
	this->clearTones();
}

static double W_to_dBm(double input) {
	return 10 * std::log10(input) + 30;
}

static double dBm_to_W(double input) {
	return std::pow(10, (input - 30) / 10);
}

static std::vector<double> to_dBm(std::vector<double> input) {
	std::vector<double> output = std::vector<double>(input.size());
	for (int i = 0; i < output.size(); ++i) {
		output[i] = W_to_dBm(input[i]);
	}
	return output;
}

static std::vector<double> to_W(std::vector<double> input) {
	std::vector<double> output = std::vector<double>(input.size());
	for (int i = 0; i < output.size(); ++i) {
		output[i] = dBm_to_W(input[i]);
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

void SpectrumAnalyzer::addToneRef(const tone* tone_ref) {
	if (!tone_ref) return;
	auto& tones = m_current_spectrum.tones;
	if (std::find(tones.begin(), tones.end(), tone_ref) == tones.end()) {
		tones.push_back(tone_ref);
		LOG_INFO("Add new tone: %.2f / %.2f (%d active)", tone_ref->first, tone_ref->second, tones.size());
	}
}

void SpectrumAnalyzer::removeToneRef(const tone* tone_ref) {
	auto& tones = m_current_spectrum.tones;
	tones.erase(std::remove(tones.begin(), tones.end(), tone_ref), tones.end());
}

void SpectrumAnalyzer::clearTones() {
	LOG_INFO("Clearing referenced tones...");
	m_current_spectrum.tones.clear();
}

void SpectrumAnalyzer::draw(const char* title, bool* p_open) {
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

	for (const auto* tone_ref : m_current_spectrum.tones) {
		if (!tone_ref) continue;

		const auto& freq_Hz = tone_ref->first;
		const auto& power_dBm = tone_ref->second;
		int bin_idx = static_cast<int>((freq_Hz - m_start_freq) / m_vbw);
		if (bin_idx >= 0 && bin_idx < num_points) {
			// Convert dBm to W and add to noise power
			m_current_spectrum.noise_power_W[bin_idx] += std::pow(10.0, (power_dBm - 30.0) / 10.0);
		}
	}
}

void SpectrumAnalyzer::updateNoiseLevel() {
	m_noise_level_dBm = MIN_POWER + 10 * std::log10(m_rbw);
}