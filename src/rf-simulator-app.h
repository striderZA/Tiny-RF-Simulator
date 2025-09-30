#pragma once
#include <vector>

struct Spectrum {
	std::vector<double> frequencies;
	std::vector<double> signal;
	std::vector<double> noise;
};

class SignalGenerator {
public:
	SignalGenerator(float freq = 1.0f, float amp = 1.0f, float noise_floor = 0.01f);

	void setFrequency(float freq) { m_freq = freq; }
	void setAmplitude(float amp) { m_amp = amp; }
	void setNoiseFloow(float noise_floor) { m_noise_floor = noise_floor; }

	float getFrequency() const { return m_freq; }
	float getAmplitude() const { return m_amp; }
	float getNoiseFloor() const { return m_noise_floor; }

	void process(const Spectrum& input, Spectrum& output);

private:

	float m_freq;
	float m_amp;
	float m_noise_floor;

	static constexpr double min_freq = 0.0;
	static constexpr double max_freq = 100.0;
	static constexpr size_t num_bins = 1024;
};

class RfSimulatorApp {
public:
	RfSimulatorApp();
	void onGui();
private:
	SignalGenerator m_siggen;
	bool m_show_spectrum = false;
	Spectrum m_current_spectrum;
};