#include "signal_generator.h"
#include "imgui.h"
#include "logging.h"

SignalGenerator::SignalGenerator(int id) : m_id(id), m_active_tone(std::make_pair<double, double>(0, 0)), m_measurement_active(false) {
	LOG_INFO("Signal generator setup complete!");
}

void SignalGenerator::draw(const char* title, bool* p_open) {
	if (ImGui::Begin(title, p_open)) {
		ImGui::Checkbox("Measure", &m_measurement_active);
		ImGui::InputDouble("f (Hz)", &m_active_tone.first, 10e6, 1000e6, "%.0f");
		ImGui::InputDouble("P (dBm)", &m_active_tone.second, 1, 5, "%.0f");
		ImGui::End();
	}
}