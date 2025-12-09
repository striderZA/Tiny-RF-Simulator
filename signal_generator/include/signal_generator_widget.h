#pragma once
#include "signal_generator_engine.h"

class SignalGeneratorWidget {
public:
	SignalGeneratorWidget(SignalGeneratorEngine& engine);
	void draw(const char* title, bool* p_open = nullptr);

	bool measurementActive() const { return m_measurement_active; }

private:
	SignalGeneratorEngine& m_engine;
	bool m_measurement_active;
};