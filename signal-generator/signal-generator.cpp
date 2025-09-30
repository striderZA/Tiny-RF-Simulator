#include "signal-generator.h"
#include <algorithm>
#include <cmath>

SignalGenerator::SignalGenerator(int id, float freq, float amp, float noise_floor) : m_id(id), m_freq(freq), m_amp(amp), m_noise_floor(noise_floor) {}

void SignalGenerator::setFrequency(float freq) {
	m_freq = freq;
	LOG_INFO("Updated frequency: %.2f Hz", m_freq);
}

void SignalGenerator::setAmplitude(float amp) {
	m_amp = amp;
	LOG_INFO("Updated amplitude: %.2f", m_amp);
}

void SignalGenerator::setNoiseFloor(float noise_floor) {
	m_noise_floor = noise_floor;
	LOG_INFO("Updated noise floor: %.2f", m_noise_floor);
}

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