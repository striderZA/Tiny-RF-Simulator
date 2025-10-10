#pragma once
#include <vector>
#include "logging.h"
#include "SpectrumAnalyzer.h"

class RfSimulatorApp {
public:
	RfSimulatorApp();
	void onGui();

private:
	SpectrumAnalyzer m_spectrum_analyzer;
	bool m_show_log = true;
};