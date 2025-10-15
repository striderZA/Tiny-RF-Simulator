#pragma once
#include "common.h"
#include "logging.h"

class SignalGenerator {
public:
	SignalGenerator(int id);

	void updateTone(tone input_tone);
	void setupTone(const char* title, bool* p_open);

	int id() const { return m_id; }

private:
	int m_id;
	tone m_active_tone;
};