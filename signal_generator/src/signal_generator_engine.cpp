#include "signal_generator_engine.h"
#include <cmath>

SignalGeneratorEngine::SignalGeneratorEngine(int id)
    : m_id(id), m_active_tone(std::make_pair<int, double>(0, -60.0)) {
    const double start_Hz = MIN_FREQ;
    const double stop_Hz = MAX_FREQ;
    const double step_Hz = 10e6;
    int n = static_cast<int>((stop_Hz - start_Hz) / step_Hz);

    m_node.output.frequencies.resize(n);

    for (int i = 0; i < n; ++i) {
        m_node.output.frequencies[i] = start_Hz + i * step_Hz;
    }

    m_node.output.noise_W.assign(m_node.output.frequencies.size(), 0.0);
    m_node.output.noise_added_W.assign(m_node.output.frequencies.size(), 0.0);
    m_node.output.computeTotalNoise();
}

void SignalGeneratorEngine::update(double dt) {

    auto &in = m_node.input;
    auto &out = m_node.output;

    out.tones.clear();
    Spectrum::Tone t;
    t.freq_Hz = static_cast<double>(m_active_tone.first);
    t.power_dBm = m_active_tone.second;
    out.tones.push_back(t);

    size_t n = out.frequencies.size();
    out.noise_added_W.assign(n, 0.0);

    out.computeTotalNoise();
}
