#pragma once
#include <vector>
#include <memory>
#include "logging.h"
#include "SpectrumAnalyzer.h"
#include "SignalGenerator.h"

enum class InputSignals : int {
	G0 = 0,
	G1,
	G2,
	G3
};

class RfSimulatorApp {
public:
	RfSimulatorApp();
	void onGui();
	void drawSignalGenerators(const char* title, bool* p_open);

private:
	SpectrumAnalyzer m_spectrum_analyzer;
	std::vector<std::unique_ptr<SignalGenerator>> m_signal_generators;
	bool m_show_log = true;
};