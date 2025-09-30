#pragma once

#include <vector>
#include "common.h"

#include "implot.h"

class SpectrumAnalyzer {
public:
	SpectrumAnalyzer();
	void analyze(const Spectrum& input);
	void draw(const char* title, bool* p_open);

	void setStartFrequency(double start_freq) { m_start_freq = start_freq; }
	void setStopFrequency(double stop_freq) { m_stop_freq = stop_freq; }

private:
	Spectrum m_current_spectrum;
	bool m_show_spectrum = false;

	double m_start_freq = MIN_FREQ;
	double m_stop_freq = MAX_FREQ;
};