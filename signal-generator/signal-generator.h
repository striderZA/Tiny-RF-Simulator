#pragma once

#include "logging.h"
#include "common.h"
#include <vector>

class SignalGenerator {
public:
	SignalGenerator(int id, float freq = 1.0f, float amp = 1.0f, float noise_floor = 0.01f);

	void setFrequency(float freq);
	void setAmplitude(float amp);
	void setNoiseFloor(float noise_floor);

	void process(const Spectrum& input, Spectrum& output);

	static constexpr double min_freq = -100.0;
	static constexpr double max_freq = 100.0;
	static constexpr size_t num_bins = 1024;

	int m_id;
	float m_freq;
	float m_amp;
	float m_noise_floor;

private:
};