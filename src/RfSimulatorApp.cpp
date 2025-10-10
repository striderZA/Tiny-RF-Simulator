#include "RfSimulatorApp.h"
#include "imgui.h"
#include <implot.h>
#include <cmath>
#include <algorithm>

RfSimulatorApp::RfSimulatorApp() : m_spectrum_analyzer() {
	LOG_INFO("Application initialized!");
}

void RfSimulatorApp::onGui() {
	if (m_show_log) {
		ShowAppLog(&m_show_log);
	}

	m_spectrum_analyzer.update("Spectrum", nullptr);
}