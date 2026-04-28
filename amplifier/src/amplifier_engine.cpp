#include "amplifier_engine.h"
#include "common.h"


AmplifierEngine::AmplifierEngine(int id) : m_id(id) {}

void AmplifierEngine::update(double dt) {
    auto &in = m_node.input;
    auto &out = m_node.output;

    if (!in.frequencies.empty()) {
        out.frequencies = in.frequencies;
    } else if (out.frequencies.size() < 2) {
        const double start_Hz = MIN_FREQ;
        const double stop_Hz = MAX_FREQ;
        if (m_f_step_Hz <= 0) m_f_step_Hz = 10e6;
        int n = static_cast<int>((stop_Hz - start_Hz) / m_f_step_Hz);
        if (n < 2) {
            n = 2;
        }
        out.frequencies.resize(n);
        for (int i = 0; i < n; ++i) {
            out.frequencies[i] = start_Hz + i * m_f_step_Hz;
        }
    }

    out.tones = in.tones;
    for (auto &t : out.tones) {
        t.power_dBm += m_gain_dB;
    }

    const size_t N = out.frequencies.size();
    if (N < 2) {
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
        return;
    }

    double G = dbToLinear(m_gain_dB);
    double added_density = addedNoiseDensity_W_per_Hz(m_nf_dB, G);

    out.noise_W.assign(N, 0.0);
    if (in.noise_total_W.size() == 1) {
        // Uniform noise density from a pure source (e.g. generator with no grid)
        double nin = in.noise_total_W[0];
        for (size_t i = 0; i < N; ++i) {
            out.noise_W[i] = G * nin;
        }
    } else {
        for (size_t i = 0; i < N; ++i) {
            double nin = (i < in.noise_total_W.size() ? in.noise_total_W[i] : 0.0);
            out.noise_W[i] = G * nin;
        }
    }

    out.noise_added_W.resize(N);
    if (added_density <= 0.0) {
        out.noise_added_W.assign(N, 0.0);
    } else {
        out.noise_added_W.assign(N, added_density);
    }
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i) {
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }
}
