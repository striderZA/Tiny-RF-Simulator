#include "spectrum_analyzer_engine.h"
#include <cmath>
#include "logging_core.h"

SpectrumAnalyzerEngine::SpectrumAnalyzerEngine()
	: m_current_spectrum() {
	LOG_INFO("Constructing spectrum engine...");
}

double SpectrumAnalyzerEngine::generateNoiseSample() const {
	std::normal_distribution<double> dist(0.0, 1.0);
	static thread_local std::mt19937 gen(std::random_device{}());

	double noise = dist(gen);
	double rms_voltage = std::sqrt(4 * k * T * m_rbw * R);
	return std::pow(noise * rms_voltage, 2) / R;
}

std::vector<double> SpectrumAnalyzerEngine::generateNoiseVector() const {
	size_t length = m_current_spectrum.frequencies.size();
	std::vector<double> noiseSamples(length);
	for (int i = 0; i < length; ++i) {
		noiseSamples[i] = this->generateNoiseSample();
	}
	return noiseSamples;
}

void SpectrumAnalyzerEngine::addToneRef(const tone* tone_ref) {
	if (!tone_ref) return;
	auto& tones = m_current_spectrum.tones;
	if (std::find(tones.begin(), tones.end(), tone_ref) == tones.end()) {
		tones.push_back(tone_ref);
	}
}

void SpectrumAnalyzerEngine::updateNoiseLevel() {
	m_noise_level_dBm = MIN_POWER + 10 * std::log10(m_rbw);
}

void SpectrumAnalyzerEngine::updateSpectrum() {
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
			m_current_spectrum.noise_power_W[bin_idx] += std::pow(10.0, (power_dBm - 30.0) / 10.0);
		}
	}
}