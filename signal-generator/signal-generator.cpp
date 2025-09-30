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
	LOG_INFO("Updated amplitude: %.2f dBm", m_amp);
}

void SignalGenerator::setNoiseFloor(float noise_floor) {
	m_noise_floor = noise_floor;
	LOG_INFO("Updated noise floor: %.2f dBm", m_noise_floor);
}

void SignalGenerator::process(const Spectrum& input, Spectrum& output)
{
	if (output.frequencies.empty()) {
		output.frequencies.resize(NUM_BINS);
		double bin_bw = (MAX_FREQ - MIN_FREQ) / (NUM_BINS - 1);
		for (size_t i = 0; i < NUM_BINS; ++i) {
			output.frequencies[i] = MIN_FREQ + i * bin_bw;
		}
	}

	if (output.signal.empty()) output.signal.assign(NUM_BINS, 0.0);
	if (output.noise.empty()) output.noise.assign(NUM_BINS, 0.0);

	double signal_power_watts = std::pow(10.0, (m_amp - 30.0) / 10.0);  // dBm to watts
	double noise_power_watts = std::pow(10.0, (m_noise_floor - 30.0) / 10.0);  // dBm to watts

	size_t closest_bin = static_cast<size_t>(std::round((m_freq - MIN_FREQ) / ((MAX_FREQ - MIN_FREQ) / (NUM_BINS - 1))));

	if (closest_bin < NUM_BINS) {
		output.signal[closest_bin] += signal_power_watts;
	}

	for (size_t i = 0; i < NUM_BINS; ++i) {
		output.noise[i] += noise_power_watts;
	}

	output.noise.resize(NUM_BINS, m_noise_floor);
}