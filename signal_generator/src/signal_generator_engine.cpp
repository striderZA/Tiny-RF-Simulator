#include "signal_generator_engine.h"
#include <cmath>

SignalGeneratorEngine::SignalGeneratorEngine(int id)
    : m_id(id), m_active_tone(std::make_pair<int, double>(0, -60.0)) {
    const double start_Hz = MIN_FREQ;
    const double stop_Hz = MAX_FREQ;
    const double step_Hz = 100e6;
    int n = static_cast<int>((stop_Hz - start_Hz) / step_Hz);
