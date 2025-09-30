#pragma once
#include <vector>
#include "logging.h"
#include "signal-generator.h"
#include "spectrum-analyzer.h"

class RfSimulatorApp {
public:
	RfSimulatorApp();
	void onGui();

private:
	std::vector<SignalGenerator> m_signal_generators;
	SpectrumAnalyzer m_spectrum_analyzer;
	int m_selected_port = 0;
	bool m_show_log = true;
	const int NUM_GENERATORS = 1;
};