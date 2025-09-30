#pragma once
#include <vector>
#include "logging.h"

struct Spectrum {
	std::vector<double> frequencies;
	std::vector<double> signal;
	std::vector<double> noise;
};

class SignalGenerator {
public:
	SignalGenerator(float freq = 1.0f, float amp = 1.0f, float noise_floor = 0.01f);

	void setFrequency(float freq);
	void setAmplitude(float amp);
	void setNoiseFloor(float noise_floor);

	void process(const Spectrum& input, Spectrum& output);

	static constexpr double min_freq = -100.0;
	static constexpr double max_freq = 100.0;
	static constexpr size_t num_bins = 1024;

	float m_freq;
	float m_amp;
	float m_noise_floor;

private:
};

class RfSimulatorApp {
public:
	RfSimulatorApp();
	void onGui();
private:
	SignalGenerator m_siggen;
	bool m_show_spectrum = false;
	Spectrum m_current_spectrum;
	bool m_show_log = true;
};