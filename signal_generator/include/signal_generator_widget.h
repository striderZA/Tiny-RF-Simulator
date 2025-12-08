#pragma once
#include "signal_generator_engine.h"

class SignalGeneratorWidget {
	public:
		SignalGeneratorWidget(SignalGeneratorEngine& engine);
		void draw(const char* title, bool* p_open = nullptr);

private:
	SignalGeneratorEngine& m_engine;
};