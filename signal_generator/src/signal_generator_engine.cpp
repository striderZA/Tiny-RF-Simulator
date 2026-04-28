#include "signal_generator_engine.h"
#include "common.h"
#include <cmath>

SignalGeneratorEngine::SignalGeneratorEngine(int id)
    : m_id(id), m_active_tone(std::make_pair<int, double>(0, -60.0)) {
    // Generator is a pure source: flat thermal noise density, no frequency grid.
    m_node.input.noise_total_W = {k * T};
}

void SignalGeneratorEngine::setToneFrequency(double frequency) {
    // Snap to the fixed 10 MHz spectrum grid so the tone always lands on a display point.
    constexpr double grid_step = 10e6;
    int bin = static_cast<int>(std::round((frequency - MIN_FREQ) / grid_step));
    m_active_tone.first = MIN_FREQ + bin * grid_step;
}

void SignalGeneratorEngine::update(double dt) {
    auto &in = m_node.input;
    auto &out = m_node.output;

    out.tones.clear();
    Spectrum::Tone t;
    t.freq_Hz = m_active_tone.first;
    t.power_dBm = m_active_tone.second;
    out.tones.push_back(t);

    // Pure source: no frequency grid of its own.  Downstream components
    // (amplifiers / spectrum analyser) create the display grid.
    // Noise density is passed as a single-element vector (uniform across spectrum).
    out.frequencies.clear();
    out.noise_W = in.noise_total_W;
    out.noise_added_W.assign(out.noise_W.size(), 0.0);
    out.noise_total_W = out.noise_W;
}
