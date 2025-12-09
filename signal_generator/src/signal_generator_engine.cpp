#include "signal_generator_engine.h"

SignalGeneratorEngine::SignalGeneratorEngine(int id)
    : m_id(id), m_active_tone(std::make_pair<int, double>(0, 0)) {}
