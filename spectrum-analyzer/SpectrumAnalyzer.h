#pragma once

#include <vector>
#include <random>
#include "common.h"
#include "implot.h"

class SpectrumAnalyzer {
public:
	SpectrumAnalyzer();
	void analyze(const Spectrum& input);
	void draw(const char* title, bool* p_open);
	void updateSpectrum();

	void setStartFrequency(double start_freq) { m_start_freq = start_freq; }
	void setStopFrequency(double stop_freq) { m_stop_freq = stop_freq; }
	void setMinPower(double min_power) { m_min_power = min_power; }
	void setMaxPower(double max_power) { m_max_power = max_power; }
	void setVideoBw(double vbw) { m_vbw = vbw; }
	void setResBw(double rbw) { m_rbw = rbw; }

	double startFrequency() const { return m_start_freq; }
	double stopFrequency() const { return m_stop_freq; }
	double minPower() const { return m_min_power; }
	double maxPower() const { return m_max_power; }
	double vbw() const { return m_vbw; }
	double rbw() const { return m_rbw; }

	void updateNoiseLevel();
	double generateNoiseSample();
	std::vector<double> generateNoiseVector();
private:
	Spectrum m_current_spectrum;
	bool m_show_spectrum = false;

	double m_start_freq = MIN_FREQ;
	double m_stop_freq = MAX_FREQ;
	double m_min_power = MIN_POWER;
	double m_max_power = MAX_POWER;

	double m_vbw = DEFAULT_VBW;
	double m_rbw = DEFAULT_RBW;
	double m_noise_level_dBm = MIN_POWER;
};