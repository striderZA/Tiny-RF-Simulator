#include "adc_engine.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
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
    for (const auto &tone : source_tones) {
        const double f_complex = alias_frequency(tone.freq_Hz - nco_Hz, Fs);
        if (f_complex < -output_edge || f_complex >= output_edge)
            continue;

        const double power_W = 0.001 * std::pow(10.0, tone.power_dBm / 10.0);
        const double phase_rad = tone.phase_deg * std::numbers::pi / 180.0;
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
    double nsd_W_per_Hz = 0.001 * dbToLinear(m_nsd_dBm_per_Hz);
    out.noise_W.assign(N, 0.0);
    out.noise_added_W.assign(N, nsd_W_per_Hz);
    out.noise_total_W.resize(N, 0.0);
    out.phase_deg.assign(N, 0.0);

    const double nco_Hz = m_nco_fs_fraction * m_fs_Hz;
    for (int i = 0; i < N; ++i) {
        double f_out = out.frequencies[i];
        double f_a = alias_frequency(f_out + nco_Hz, m_fs_Hz);
        // Input noise grid is single-sided [0, Fs/2); real-input noise is
        // conjugate-symmetric, so sample the mirrored |f_a| bin when negative.
        if (f_a < 0.0)
            f_a = -f_a;

        double noise_psd = 0.0;
        if (!input->noise_total_W.empty() && !input->frequencies.empty()) {
            size_t best_j = 0;
            double best_dist = std::numeric_limits<double>::max();
            for (size_t j = 0; j < input->frequencies.size() && j < input->noise_total_W.size();
                 ++j) {
                double dist = std::abs(input->frequencies[j] - f_a);
                if (dist < best_dist) {
                    best_dist = dist;
                    best_j = j;
                }
            }
            noise_psd = input->noise_total_W[best_j];
        }
        out.noise_W[i] += noise_psd;
    }

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
    m_fs_Hz =
        j.contains("sample_rate_Hz") ? j["sample_rate_Hz"].get<double>() : j.value("fs_Hz", 1e9);
    m_nsd_dBm_per_Hz = j.value("nsd_dBm_per_Hz", -155.0);
    if (j.contains("decimation"))
        setDecimation(j["decimation"].get<int>());
    if (j.contains("nco_fs_fraction"))
        setNcoFsFraction(j["nco_fs_fraction"].get<double>());
    m_dirty = true;
}

std::string AdcEngine::hoverSummary() const {
    return "Fs: " + std::to_string(m_fs_Hz / 1e6) +
           " MHz | NSD: " + std::to_string(m_nsd_dBm_per_Hz) + " dBm/Hz";
}
