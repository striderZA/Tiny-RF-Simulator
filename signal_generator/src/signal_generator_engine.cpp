#include "signal_generator_engine.h"
#include "common.h"
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

    double binWidth = out.frequencies[1] - out.frequencies[0];

    double G = dbToLinear(m_gain_dB);
    double F = dbToLinear(m_nf_dB);

    double added_per_bin = addedNoisePerBin_W(m_nf_dB, m_nf_dB, binWidth);

    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double nin = (i < in.noise_total_W.size() ? in.noise_total_W[i] : 0.0);
        out.noise_W[i] = G * nin;
    }


    out.noise_added_W.assign(N, added_per_bin);

    out.noise_total_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {

        double awgn = out.thermalNoisePower_W(binWidth);

        out.noise_total_W[i] = out.noise_W[i] +       // input noise after gain
                               out.noise_added_W[i] + // added noise from NF
                               awgn;                  // thermal randomness
    }
}
