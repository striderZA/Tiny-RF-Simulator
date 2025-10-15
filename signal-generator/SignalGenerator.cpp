#include "SignalGenerator.h"
#include "imgui.h"

SignalGenerator::SignalGenerator(int id) : m_id(id), m_active_tone(std::make_pair<int, double>(0, 0)) {}

void SignalGenerator::setupTone(const char* title, bool* p_open) {
	if (ImGui::Begin(title, p_open)) {
		ImGui::InputInt("Tone index", &m_active_tone.first, 1, 100);
		ImGui::InputDouble("Tone amplitude (dBm)", &m_active_tone.second, 1, 5, "%.0f");
		ImGui::End();
	}
}