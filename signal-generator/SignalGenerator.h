#pragma once
#include "common.h"
#include "logging.h"

class SignalGenerator {
public:
	SignalGenerator(int id);

	void setup(const char* title, bool* p_open);

	int id() const { return m_id; }
	tone activeTone() const { return m_active_tone; }

private:
	int m_id;
	tone m_active_tone;
};