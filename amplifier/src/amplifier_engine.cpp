#include "amplifier_engine.h"
#include "common.h"
#include <random>

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
    double added_per_bin_mean = addedNoisePerBin_W(m_nf_dB, G, m_f_step_Hz);

    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double nin = (i < in.noise_total_W.size() ? in.noise_total_W[i] : 0.0);
        out.noise_W[i] = G * nin;
    }

    out.noise_added_W.resize(N);
    if (added_per_bin_mean <= 0.0) {
        // no added noise => fill zeros
        out.noise_added_W.assign(N, 0.0);
    } else {
        static thread_local std::mt19937 gen(std::random_device{}());
        // choose a reasonable stddev (10% of mean here). Tweak if you want more/less jitter.
        double jitter_std = 0.1 * added_per_bin_mean;
        std::normal_distribution<double> add_dist(added_per_bin_mean, jitter_std);
        for (size_t i = 0; i < N; ++i) {
            double sample = add_dist(gen);
            out.noise_added_W[i] = (sample < 0.0) ? 0.0 : sample;
        }
    }
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i) {
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }
}
