#pragma once

#include <vector>
#include "common.h"

#include "implot.h"

class SpectrumAnalyzer {
public:
	SpectrumAnalyzer();
	void analyze(const Spectrum& input);
	void draw(const char* title, bool* p_open);
private:
	Spectrum m_current_spectrum;
	bool m_show_spectrum = false;
};