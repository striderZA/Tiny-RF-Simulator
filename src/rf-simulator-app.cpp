#include "rf-simulator-app.h"
#include "imgui.h"
#include <implot.h>
RfSimulatorApp::RfSimulatorApp() : m_siggen() {
	Spectrum dummy_input;
	m_siggen.process(dummy_input, m_current_spectrum);
}

void RfSimulatorApp::onGui() {
	static bool showDemo = true;
	if (showDemo == true) {
		ImGui::ShowDemoWindow(&showDemo);
	}
	

	static bool showImPlotDemo = true;
	if (showImPlotDemo == true){
		ImPlot::ShowDemoWindow(&showImPlotDemo);
	}

	//if (ImPlot::BeginPlot("Example Plot")) {
	//	static float x_data[] = { 1.0f, 2.0f, 3.0f, 4.0f };
	//	static float y_data[] = { 1.0f, 4.0f, 2.0f, 3.0f };
	//	ImPlot::PlotLine("Line", x_data, y_data, 4);
	//	ImPlot::EndPlot();
	//}	
}