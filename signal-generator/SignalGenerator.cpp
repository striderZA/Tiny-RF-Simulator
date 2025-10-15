#include "SignalGenerator.h"
#include "imgui.h"

SignalGenerator::SignalGenerator(int id) : m_id(id), m_active_tone(std::make_pair<int, double>(0,0)) {}

void SignalGenerator::updateTone(tone input_tone){
	m_active_tone = input_tone;
}

void SignalGenerator::setupTone(const char* title, bool* p_open){
	if (ImGui::Begin(title, p_open)) {
		ImGui::InputInt("Tone index", &m_active_tone.first, 1, 100);
		ImGui::InputDouble("Tone amplitude (dBm)", &m_active_tone.second, 1, 5, "%.0f");
		if (ImGui::Button("Update")) {
			this->updateTone(m_active_tone);
			LOG_INFO("Update generator %d: Frequency bin = %d, amplitude = %.2f dBm", m_id, m_active_tone.first, m_active_tone.second);
		}
		ImGui::End();
	}
}
