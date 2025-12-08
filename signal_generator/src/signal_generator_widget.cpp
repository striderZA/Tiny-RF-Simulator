#include "signal_generator_widget.h"
#include "imgui.h"

SignalGeneratorWidget::SignalGeneratorWidget(SignalGeneratorEngine& engine) : m_engine(engine) {
}

void SignalGeneratorWidget::draw(const char* title, bool* p_open) {
	if (ImGui::Begin(title, p_open)) {
		int tone_index = m_engine.activeTone().first;
		double amplitude = m_engine.activeTone().second;

		if (ImGui::InputInt("Tone index", &tone_index, 1, 100)) {
			m_engine.setToneIndex(tone_index);
		}

		if (ImGui::InputDouble("Tone amplitude (dBm)", &amplitude, 1, 5, "%.0f")) {
			m_engine.setToneAmplitude(amplitude);
		}

		ImGui::End();
	}
}