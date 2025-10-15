#pragma once
#include <vector>
#include "logging.h"
#include "SpectrumAnalyzer.h"
#include "SignalGenerator.h"

class RfSimulatorApp {
public:
	RfSimulatorApp();
	void onGui();

private:
	SpectrumAnalyzer m_spectrum_analyzer;
	SignalGenerator m_signal_generator;
	bool m_show_log = true;
};