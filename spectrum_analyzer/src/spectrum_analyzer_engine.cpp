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
	for (size_t i = 0; i < length; ++i) {
		noiseSamples[i] = generateNoiseSample();
	}
	return noiseSamples;
}

void SpectrumAnalyzerEngine::addTone(tone t) {
	//m_current_spectrum.tones.push_back(t);
}

void SpectrumAnalyzerEngine::updateNoiseLevel() {
	m_noise_level_dBm = MIN_POWER + 10 * std::log10(m_rbw);
}

void SpectrumAnalyzerEngine::updateSpectrum() {
	int num_points = std::round((m_stop_freq - m_start_freq) / m_vbw);

	m_current_spectrum.frequencies.resize(num_points);
	m_current_spectrum.noise_power_W.resize(num_points);

	for (int i = 0; i < num_points; ++i)
		m_current_spectrum.frequencies[i] = m_start_freq + i * m_vbw;

	m_current_spectrum.noise_power_W = generateNoiseVector();

	//for (auto& tone_idx : m_current_spectrum.tones) {
	//	m_current_spectrum.noise_power_W[tone_idx.first] +=
	//		std::pow(10, (tone_idx.second - 30) / 10);
	//}
}