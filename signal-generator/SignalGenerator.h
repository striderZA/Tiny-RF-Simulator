#pragma once
#include "logging.h"

class SignalGenerator {
public:
	SignalGenerator(int id);

	int id() const { return m_id; }

private:
	int m_id;
	tone m_active_tone;
};