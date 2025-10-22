#include "signal_generator.h"
#include "imgui.h"
#include "logging.h"

SignalGenerator::SignalGenerator(int id) : m_id(id), m_active_tone(std::make_pair<int, double>(0, 0)) {
	LOG_INFO("Signal generator setup complete!");
}

void SignalGenerator::setup(const char* title, bool* p_open) {
	if (ImGui::Begin(title, p_open)) {
		ImGui::Checkbox("Measure", &m_measurement_active);
		ImGui::InputInt("Tone index", &m_active_tone.first, 1, 100);
		ImGui::InputDouble("Tone amplitude (dBm)", &m_active_tone.second, 1, 5, "%.0f");
		ImGui::End();
	}
}
