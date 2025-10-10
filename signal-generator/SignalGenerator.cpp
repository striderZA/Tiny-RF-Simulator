#include "SignalGenerator.h"
#include <algorithm>
#include <cmath>

SignalGenerator::SignalGenerator(int id, float freq, float amp, float noise_floor) : m_id(id), m_freq(freq), m_amp(amp), m_noise_floor(noise_floor) {}

void SignalGenerator::update(const char* title, bool* p_open)
{
}

void SignalGenerator::initialize()
{
}

void SignalGenerator::setFrequency(float freq) {
	m_freq = freq;
}

void SignalGenerator::setAmplitude(float amp) {
	m_amp = amp;
}

void SignalGenerator::setNoiseFloor(float noise_floor) {
	m_noise_floor = noise_floor;
}

void SignalGenerator::update(const Spectrum& input, Spectrum& output)
{
}