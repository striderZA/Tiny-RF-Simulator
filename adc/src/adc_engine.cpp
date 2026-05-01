#include "adc_engine.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.h"
#include "logging_core.h"

#include <kiss_fft.h>

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

// ---- Hann window ----

static void apply_hann(std::vector<std::complex<double>>& signal) {
    size_t N = signal.size();
    for (size_t n = 0; n < N; ++n) {
        double w = 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * n / (N - 1)));
        signal[n] *= w;
    }
}

static double hann_window_energy(size_t N) {
    // sum_{n=0}^{N-1} w[n]^2 for Hann: w[n] = 0.5 - 0.5*cos(2πn/(N-1))
    // Equivalent to 3N/8 for large N, but compute exactly
    double sum = 0.0;
    for (size_t n = 0; n < N; ++n) {
        double w = 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * n / (N - 1)));
        sum += w * w;
    }
    return sum;
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

    // -- Step 3: LPF design (cached) — cutoff = Fs_out/2 / (Fs/2) = 1/D
    double cutoff_norm = 1.0 / m_decim;
    int num_taps = 16 * m_decim + 1;
    if (m_fs_Hz != m_lpf_cached_fs || m_decim != m_lpf_cached_decim) {
        m_lpf_coeffs = design_lpf(cutoff_norm, num_taps);
        m_lpf_cached_fs = m_fs_Hz;
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
    size_t N_fft = m_iq_output.samples.size();

    if (N_fft == 0) {
        auto& out = m_node.outputs[0];
        out.frequencies.clear();
        out.phase_deg.clear();
        out.noise_W.clear();
        out.noise_added_W.clear();
        out.tones.clear();
        out.computeTotalNoise();
        return;
    }

    // Apply Hann window, then compute FFT via KissFFT
    auto windowed = m_iq_output.samples;
    apply_hann(windowed);
    double win_energy = hann_window_energy(N_fft);

    auto* fwd = kiss_fft_alloc(static_cast<int>(N_fft), 0, nullptr, nullptr);
    if (!fwd) {
        LOG_ERROR("ADC [adc%d]: kiss_fft_alloc failed for N=%zu.", m_id, N_fft);
        auto& out = m_node.outputs[0];
        out.frequencies.clear();
        out.phase_deg.clear();
        out.noise_W.clear();
        out.noise_added_W.clear();
        out.tones.clear();
        out.computeTotalNoise();
        return;
    }

    std::vector<kiss_fft_cpx> fft_in(N_fft), fft_out(N_fft);
    for (size_t i = 0; i < N_fft; ++i) {
        fft_in[i].r = static_cast<float>(windowed[i].real());
        fft_in[i].i = static_cast<float>(windowed[i].imag());
    }
    kiss_fft(fwd, fft_in.data(), fft_out.data());
    kiss_fft_free(fwd);

    auto& out = m_node.outputs[0];
    out.frequencies.resize(N_fft);
    out.phase_deg.resize(N_fft);
    out.noise_W.resize(N_fft);
    out.noise_added_W.assign(N_fft, 0.0);
    out.tones.clear();
    double fs_out = m_iq_output.sample_rate_Hz;
    for (size_t i = 0; i < N_fft; ++i) {
        out.frequencies[i] = -fs_out / 2.0
                             + (static_cast<double>(i) / N_fft) * fs_out;
        size_t k = (i + N_fft / 2) % N_fft;
        double re = fft_out[k].r;
        double im = fft_out[k].i;
        out.phase_deg[i] = std::atan2(im, re) * 180.0 / std::numbers::pi;
        // PSD (W/Hz) = |X[k]|² / (win_energy × fs_out)
        out.noise_W[i] = (re * re + im * im) / (win_energy * fs_out);
    }
    out.computeTotalNoise();
}
