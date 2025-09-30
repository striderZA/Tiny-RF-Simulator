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

	int id() const { return m_id; }
	float freq() const { return m_freq; }
	float amp() const { return m_amp; }
	float noise() const { return m_noise_floor; }

private:
	int m_id;
	float m_freq;
	float m_amp;
	float m_noise_floor;
};