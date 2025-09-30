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
	output.frequencies.resize(NUM_BINS);
	double bin_bw = (MAX_FREQ - MIN_FREQ) / (NUM_BINS - 1);
	for (size_t i = 0; i < NUM_BINS; ++i) {
		output.frequencies[i] = MIN_FREQ + i * bin_bw;
	}

	output.signal.assign(NUM_BINS, 0.0);
	output.noise.assign(NUM_BINS, m_noise_floor);

	output.signal.resize(NUM_BINS, 0.0);
	size_t closest_bin = static_cast<size_t>(std::round((m_freq - MIN_FREQ) / bin_bw));
	if (closest_bin < NUM_BINS) {
		output.signal[closest_bin] = m_amp;
	}

	output.noise.resize(NUM_BINS, m_noise_floor);
}