#pragma once
#include <vector>
#include <random>
#include "common.h"

class SpectrumAnalyzerEngine {
public:
	SpectrumAnalyzerEngine();

	// Configuration API
	void setStartFrequency(double v) { m_start_freq = v; }
	void setStopFrequency(double v) { m_stop_freq = v; }
	void setMinPower(double v) { m_min_power = v; }
	void setMaxPower(double v) { m_max_power = v; }
	void setVideoBw(double v) { m_vbw = v; }
	void setResBw(double v) { m_rbw = v; }

	double startFrequency() const { return m_start_freq; }
	double stopFrequency()  const { return m_stop_freq; }
	double minPower()       const { return m_min_power; }
	double maxPower()       const { return m_max_power; }
	double vbw()            const { return m_vbw; }
	double rbw()            const { return m_rbw; }

	// DSP operations
	void updateSpectrum();
	void updateNoiseLevel();
	void addTone(tone t);

	void clearTones() { m_current_spectrum.tones.clear(); }

	const Spectrum& spectrum() const { return m_current_spectrum; }
	double noiseLevel_dBm() const { return m_noise_level_dBm; }

private:
	Spectrum m_current_spectrum;

	// Config
	double m_start_freq = MIN_FREQ;
	double m_stop_freq = MAX_FREQ;
	double m_min_power = MIN_POWER;
	double m_max_power = MAX_POWER;
	double m_vbw = DEFAULT_VBW;
	double m_rbw = DEFAULT_RBW;

	double m_noise_level_dBm = MIN_POWER;

	// Internal helpers
	double generateNoiseSample() const;
	std::vector<double> generateNoiseVector() const;
};