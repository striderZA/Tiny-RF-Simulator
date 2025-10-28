#pragma once
#include "common.h"

class SignalGenerator {
public:
	SignalGenerator(int id);

	void draw(const char* title, bool* p_open);

	int id() const { return m_id; }
	tone activeTone() const { return m_active_tone; }
	bool measurementActive() const { return m_measurement_active; }
	tone m_active_tone;

private:
	int m_id;
	bool m_measurement_active;
};