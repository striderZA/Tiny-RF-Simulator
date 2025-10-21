#pragma once

#include <vector>
#include <memory>
#include "signal_generator.h"
#include "spectrum_analyzer.h"

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

private:
	bool enable_log;
	SpectrumAnalyzer m_spectrum_analyzer;
	std::vector<std::unique_ptr<SignalGenerator>> m_signal_generators;
};