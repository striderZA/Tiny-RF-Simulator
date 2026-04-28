#include "signal_generator_engine.h"
#include "common.h"
#include <cmath>

SignalGeneratorEngine::SignalGeneratorEngine(int id)
    : m_id(id), m_active_tone(std::make_pair<int, double>(0, -60.0)) {
    rebuildFrequencyGrid();
}

void SignalGeneratorEngine::rebuildFrequencyGrid() {
    const double start_Hz = MIN_FREQ;
    const double stop_Hz = MAX_FREQ;
    constexpr double fixed_step = 10e6;
    int n = static_cast<int>((stop_Hz - start_Hz) / fixed_step);
    if (n < 2) n = 2;

    m_node.output.frequencies.resize(n);
    for (int i = 0; i < n; ++i) {
        m_node.output.frequencies[i] = start_Hz + i * fixed_step;
    }

    m_node.output.noise_W.assign(n, 0.0);
    m_node.output.noise_added_W.assign(n, 0.0);
    // Generator is an ideal source: flat thermal noise density k*T (W/Hz)
    m_node.input.noise_total_W.assign(n, k * T);
    m_node.output.computeTotalNoise();
}

void SignalGeneratorEngine::setToneFrequency(double frequency) {
    // Snap to the fixed 10 MHz grid so the tone always lands on a display point.
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

    const size_t N = out.frequencies.size();
    if (N < 2) {
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
        return;
    }

    // Unity gain for noise density (clean source)
    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double nin = (i < in.noise_total_W.size() ? in.noise_total_W[i] : 0.0);
        out.noise_W[i] = nin;
    }

    // Generator adds no noise of its own
    out.noise_added_W.assign(N, 0.0);

    out.noise_total_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }
}
