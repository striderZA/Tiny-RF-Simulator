#include "adc_engine.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <map>
#include <numbers>

#include "common.h"
#include "logging_core.h"

// ---- Nyquist zone utilities ----

static double alias_frequency(double f_RF, double Fs) {
    double f = std::fmod(f_RF, Fs);
    if (f >= Fs / 2.0)
        f -= Fs;
    if (f < -Fs / 2.0)
        f += Fs;
    return f;
}

static std::vector<Spectrum::Tone> map_tones_to_complex(const Spectrum &input, double Fs,
                                                        int decimation, double nco_fs_fraction) {
    const auto source_tones =
        input.is_complex_baseband ? input.tones : conjugateSymmetricExpand(input.tones);
    std::map<double, std::complex<double>> accumulated;
    const double nco_Hz = nco_fs_fraction * Fs;
    const double output_edge = Fs / (2.0 * decimation);
    // nco_Hz and each alias are rounded separately, so coincident images can differ by a few
    // ulps. Merge keys within a tolerance far below any tone spacing the simulator resolves.
    const double merge_tol_Hz = Fs * 1e-12;
    for (const auto &tone : source_tones) {
        const double f_alias = alias_frequency(tone.freq_Hz - nco_Hz, Fs);
        if (f_alias < -output_edge || f_alias >= output_edge)
            continue;

        const double power_W = 0.001 * std::pow(10.0, tone.power_dBm / 10.0);
        const double phase_rad = tone.phase_deg * std::numbers::pi / 180.0;
        auto it = accumulated.lower_bound(f_alias - merge_tol_Hz);
        const double f_complex =
            (it != accumulated.end() && it->first <= f_alias + merge_tol_Hz) ? it->first : f_alias;
        accumulated[f_complex] += std::polar(std::sqrt(power_W), phase_rad);
    }

    std::vector<Spectrum::Tone> result;
    result.reserve(accumulated.size());
    for (const auto &[freq_Hz, amplitude] : accumulated) {
        const double power_W = std::norm(amplitude);
        if (power_W <= 0.0)
            continue;
        result.push_back(
            {freq_Hz, 10.0 * std::log10(power_W / 0.001),
             std::atan2(amplitude.imag(), amplitude.real()) * 180.0 / std::numbers::pi});
    }
    return result;
}

// Nearest-bin input noise PSD at |f_Hz|. Real-domain noise is conjugate-symmetric, so
// S(f) = S(|f|). Requires |f_Hz| <= the grid's largest magnitude; the grid is sorted ascending.
static double nearest_noise_psd(const Spectrum &input, size_t n, double f_Hz) {
    const auto first = input.frequencies.begin();
    const auto last = first + static_cast<std::ptrdiff_t>(n);
    const double t = std::abs(f_Hz);
    const auto it = std::lower_bound(first, last, t);
    size_t j = n - 1;
    if (it == first) {
        j = 0;
    } else if (it != last) {
        const size_t hi = static_cast<size_t>(it - first);
        j = (t - *(it - 1) <= *it - t) ? hi - 1 : hi;
    }
    return input.noise_total_W[j];
}

// Output noise PSD at complex frequency f_base_Hz (= f_out + f_NCO). Noise folds from every
// Nyquist zone exactly as tones do: each integer zone m contributes the input PSD at
// f_base + m*Fs, and zero beyond the input grid span.
static double folded_noise_psd(const Spectrum &input, double f_base_Hz, double Fs) {
    const size_t n = std::min(input.frequencies.size(), input.noise_total_W.size());
    if (n == 0)
        return 0.0;
    const double f_limit =
        std::max(std::abs(input.frequencies.front()), std::abs(input.frequencies[n - 1]));
    const int m_max = static_cast<int>(std::ceil((f_limit + std::abs(f_base_Hz)) / Fs));
    double sum = 0.0;
    for (int m = -m_max; m <= m_max; ++m) {
        const double f_real = f_base_Hz + m * Fs;
        if (std::abs(f_real) <= f_limit)
            sum += nearest_noise_psd(input, n, f_real);
    }
    return sum;
}

// ---- Engine methods ----

AdcEngine::AdcEngine(int id, NodeGraphEngine &graph) : ComponentEngineBase(id, graph, "ADC", 1, 1) {
    LOG_INFO("ADC [adc%d] added.", id);
}

void AdcEngine::update(double /*dt*/) {
    const Spectrum *input = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
    if (!beginUpdate(input))
        return;

    auto &out = m_node.outputs[0];

    const double output_fs = m_fs_Hz / static_cast<double>(m_decimation);
    const double output_edge = output_fs / 2.0;

    // Empty input -> empty output
    if (!input || input->frequencies.empty()) {
        out.frequencies.clear();
        out.tones.clear();
        out.noise_W.clear();
        out.noise_added_W.clear();
        out.noise_total_W.clear();
        out.phase_deg.clear();
        out.fs_Hz = output_fs;
        out.is_complex_baseband = true;
        out.bumpGeneration();
        return;
    }

    // -- Compute output grid: N bins on [-output_edge, output_edge) --
    int N = 201;
    if (input->frequencies.size() >= 2) {
        double span = input->frequencies.back() - input->frequencies.front();
        double df_in = span / (input->frequencies.size() - 1);
        N = std::max(2, static_cast<int>(std::ceil(output_fs / df_in)));
    }
    double df_out = output_fs / N;
    out.frequencies.resize(N);
    for (int i = 0; i < N; ++i)
        out.frequencies[i] = -output_edge + i * df_out;
    out.fs_Hz = output_fs;
    out.is_complex_baseband = true;

    // -- Noise mapping + NSD --
    const double nsd_W_per_Hz = 0.001 * dbToLinear(m_nsd_dBm_per_Hz);
    const double real_input_noise_scale = input->is_complex_baseband ? 1.0 : 0.5;
    out.noise_W.assign(N, 0.0);
    out.noise_added_W.assign(N, nsd_W_per_Hz * real_input_noise_scale);
    out.noise_total_W.resize(N, 0.0);
    out.phase_deg.assign(N, 0.0);

    const double nco_Hz = m_nco_fs_fraction * m_fs_Hz;
    for (int i = 0; i < N; ++i)
        out.noise_W[i] =
            folded_noise_psd(*input, out.frequencies[i] + nco_Hz, m_fs_Hz) * real_input_noise_scale;

    out.computeTotalNoise();

    // -- Tone mapping: real-domain conjugate expansion -> signed alias -> DDC -> LPF --
    out.tones = map_tones_to_complex(*input, m_fs_Hz, m_decimation, m_nco_fs_fraction);

    out.bumpGeneration();
}

nlohmann::json AdcEngine::serialize() const {
    return {{"sample_rate_Hz", m_fs_Hz},
            {"nsd_dBm_per_Hz", m_nsd_dBm_per_Hz},
            {"decimation", m_decimation},
            {"nco_fs_fraction", m_nco_fs_fraction}};
}

void AdcEngine::deserialize(const nlohmann::json &j) {
    const double fs =
        j.contains("sample_rate_Hz") ? j["sample_rate_Hz"].get<double>() : j.value("fs_Hz", 1e9);
    setFs_Hz(fs);
    m_nsd_dBm_per_Hz = j.value("nsd_dBm_per_Hz", -155.0);
    setDecimation(j.value("decimation", 2));
    setNcoFsFraction(j.value("nco_fs_fraction", 0.25));
    m_dirty = true;
}

std::string AdcEngine::hoverSummary() const {
    return "Fs: " + std::to_string(m_fs_Hz / 1e6) +
           " MHz | NSD: " + std::to_string(m_nsd_dBm_per_Hz) + " dBm/Hz";
}
