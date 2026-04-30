#include "adc_engine.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.h"
#include "logging_core.h"

// ---- Nyquist zone utilities ----

static double alias_frequency(double f_RF, double Fs) {
    double f = std::fmod(f_RF, Fs);
    if (f > Fs / 2.0)
        f = Fs - f;
    return f;
}

static int nyquist_zone(double f_RF, double Fs) {
    return static_cast<int>(f_RF / (Fs / 2.0)) + 1;
}

static bool is_inverted(double f_RF, double Fs) {
    return nyquist_zone(f_RF, Fs) % 2 == 0;
}

// ---- FIR filter utilities ----

static std::vector<double> design_lpf(double cutoff_norm, int num_taps) {
    std::vector<double> h(num_taps);
    int M = num_taps - 1;
    double alpha = 3.0;
    double i0a = 1.0 / std::cyl_bessel_i(0, alpha);

    for (int n = 0; n < num_taps; ++n) {
        if (n == M / 2) {
            h[n] = cutoff_norm;
        } else {
            double arg = std::numbers::pi * cutoff_norm * (n - M / 2.0);
            h[n] = std::sin(arg) / arg;
        }
        double t = (2.0 * n / M) - 1.0;
        double w = std::cyl_bessel_i(0, alpha * std::sqrt(1.0 - t * t)) * i0a;
        h[n] *= w;
    }
    return h;
}

static void apply_fir(const std::vector<double>& coeffs,
                      const std::vector<std::complex<double>>& input,
                      std::vector<std::complex<double>>& output) {
    int N = static_cast<int>(input.size());
    int K = static_cast<int>(coeffs.size());
    output.resize(N);
    for (int n = 0; n < N; ++n) {
        std::complex<double> sum = 0;
        for (int k = 0; k < K; ++k) {
            int idx = n - k;
            if (idx >= 0)
                sum += coeffs[k] * input[idx];
        }
        output[n] = sum;
    }
}

// ---- Iterative radix-2 FFT (Cooley-Tukey) for diagnostic output ----

static size_t next_pow2(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

static void fft(std::vector<std::complex<double>>& x) {
    size_t N = x.size();
    // Bit-reversal permutation
    for (size_t i = 1, j = 0; i < N; ++i) {
        size_t bit = N >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(x[i], x[j]);
    }
    // Iterative Cooley-Tukey
    for (size_t len = 2; len <= N; len <<= 1) {
        double angle = -2.0 * std::numbers::pi / static_cast<double>(len);
        std::complex<double> wlen(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < N; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t j = 0; j < len / 2; ++j) {
                std::complex<double> u = x[i + j];
                std::complex<double> v = x[i + j + len / 2] * w;
                x[i + j] = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

// ---- Engine methods ----

AdcEngine::AdcEngine(int id, NodeGraphEngine& graph)
    : m_id(id)
    , m_graph(&graph)
    , m_node(SignalNode{ {}, {}, false })
{
    m_graph_node_id = m_graph->addNode("ADC " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
    LOG_INFO("ADC [adc%d] added.", id);
}

int AdcEngine::inputPinId() const {
    for (const auto& n : m_graph->nodes()) {
        if (n.node_id == m_graph_node_id && !n.input_pin_ids.empty())
            return n.input_pin_ids[0];
    }
    return -1;
}

int AdcEngine::outputPinId() const {
    for (const auto& n : m_graph->nodes()) {
        if (n.node_id == m_graph_node_id && !n.output_pin_ids.empty())
            return n.output_pin_ids[0];
    }
    return -1;
}

void AdcEngine::update(double /*dt*/) {
    if (m_fs_Hz <= 0.0) return;

    const auto& input = m_node.inputs[0];

    // Skip if nothing changed since last frame
    bool changed = m_dirty;
    if (!changed && input.tones.size() == m_last_tones.size()) {
        for (size_t i = 0; i < input.tones.size(); ++i) {
            if (input.tones[i].freq_Hz != m_last_tones[i].freq_Hz
                || input.tones[i].power_dBm != m_last_tones[i].power_dBm
                || input.tones[i].phase_deg != m_last_tones[i].phase_deg) {
                changed = true;
                break;
            }
        }
    } else if (!changed) {
        changed = true;
    }
    if (!changed) return;
    m_dirty = false;
    m_last_tones = input.tones;

    int N = std::max(m_n_samples, m_decim);

    // -- Step 1: Synthesize real time-domain signal from tones + noise --
    std::vector<double> x_real(N, 0.0);
    std::mt19937 rng(42);
    std::normal_distribution<double> gauss(0.0, 1.0);

    // 1a. Tone synthesis
    for (const auto& tone : input.tones) {
        double f_dig = alias_frequency(tone.freq_Hz, m_fs_Hz);
        int sign = is_inverted(tone.freq_Hz, m_fs_Hz) ? -1 : 1;
        double power_W = 0.001 * std::pow(10.0, tone.power_dBm / 10.0);
        double amplitude = std::sqrt(2.0 * 50.0 * power_W);
        for (int n = 0; n < N; ++n) {
            double phi = tone.phase_deg * std::numbers::pi / 180.0;
            x_real[n] += amplitude * std::cos(2.0 * std::numbers::pi * sign * f_dig
                                               * n / m_fs_Hz + phi);
        }
    }

    // 1b. Noise synthesis (RF input noise + ADC NSD noise)
    double P_rf = 0.0;
    if (!input.frequencies.empty()) {
        double bin_width_Hz = input.frequencies[1] - input.frequencies[0];
        for (size_t i = 0; i < input.noise_total_W.size(); ++i)
            P_rf += input.noise_total_W[i] * bin_width_Hz;
    }
    double nsd_W_per_Hz = 0.001 * std::pow(10.0, m_nsd_dBm_per_Hz / 10.0);
    double P_adc = nsd_W_per_Hz * (m_fs_Hz / 2.0);
    double sigma = std::sqrt(P_rf + P_adc);
    for (int n = 0; n < N; ++n)
        x_real[n] += sigma * gauss(rng);

    // -- Step 2: DDC (Digital Down-Conversion) --
    double f_dig_chan = alias_frequency(m_f_channel_Hz, m_fs_Hz);
    bool inverted = is_inverted(m_f_channel_Hz, m_fs_Hz);
    double f_nco = inverted ? -f_dig_chan : f_dig_chan;

    std::vector<std::complex<double>> iq(N);
    for (int n = 0; n < N; ++n) {
        double angle = -2.0 * std::numbers::pi * f_nco * n / m_fs_Hz;
        std::complex<double> nco(std::cos(angle), std::sin(angle));
        iq[n] = x_real[n] * nco;
    }

    // -- Step 3: LPF design (cached) --
    double cutoff_norm = std::min(m_bw_Hz / (m_fs_Hz / 2.0), 1.0 / m_decim);
    int num_taps = 16 * m_decim + 1;
    if (m_fs_Hz != m_lpf_cached_fs || m_bw_Hz != m_lpf_cached_bw
        || m_decim != m_lpf_cached_decim) {
        if (cutoff_norm > 0.0 && cutoff_norm < 1.0)
            m_lpf_coeffs = design_lpf(cutoff_norm, num_taps);
        else
            m_lpf_coeffs = {1.0};
        m_lpf_cached_fs = m_fs_Hz;
        m_lpf_cached_bw = m_bw_Hz;
        m_lpf_cached_decim = m_decim;
    }

    // -- Step 4: Apply LPF + decimate --
    std::vector<std::complex<double>> filtered;
    apply_fir(m_lpf_coeffs, iq, filtered);

    int N_out = N / m_decim;
    m_iq_output.samples.resize(N_out);
    m_iq_output.sample_rate_Hz = m_fs_Hz / m_decim;
    for (int n = 0; n < N_out; ++n)
        m_iq_output.samples[n] = filtered[n * m_decim];

    // -- Step 5: Diagnostic FFT output (Spectrum) --
    size_t N_fft = next_pow2(m_iq_output.samples.size());
    auto fft_in = m_iq_output.samples;
    fft_in.resize(N_fft, {0.0, 0.0});
    fft(fft_in);

    auto& out = m_node.outputs[0];
    out.frequencies.resize(N_fft);
    out.phase_deg.resize(N_fft);
    out.noise_W.resize(N_fft);
    out.noise_added_W.assign(N_fft, 0.0);
    out.tones.clear();
    double fs_out = m_iq_output.sample_rate_Hz;
    for (size_t i = 0; i < N_fft; ++i) {
        size_t k = (i + N_fft / 2) % N_fft;
        double f = (static_cast<double>(k) / N_fft) * fs_out;
        if (f > fs_out / 2.0) f -= fs_out;
        out.frequencies[i] = f;
        out.phase_deg[i] = std::arg(fft_in[k]) * 180.0 / std::numbers::pi;
        // FFT power → PSD (W/Hz): |X[k]|² / (N_fft × fs_out)
        out.noise_W[i] = std::norm(fft_in[k])
                         / (static_cast<double>(N_fft) * fs_out);
    }
    out.computeTotalNoise();
}
