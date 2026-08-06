#include "adc_engine.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

#include "common.h"
#include "logging_core.h"

// ---- Nyquist zone utilities ----

static double alias_frequency(double f_RF, double Fs) {
    double f = std::fmod(f_RF, Fs);
    if (f > Fs / 2.0)
        f = Fs - f;
    return f;
}

// ---- Engine methods ----

AdcEngine::AdcEngine(int id, NodeGraphEngine &graph) : m_id(id), m_graph(&graph) {
    m_graph_node_id = m_graph->addNode("ADC " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
    LOG_INFO("ADC [adc%d] added.", id);
}

int AdcEngine::inputPinId() const { return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1; }

int AdcEngine::outputPinId() const { return m_graph ? m_graph->outputPinId(m_graph_node_id) : -1; }

void AdcEngine::update(double /*dt*/) {
    const Spectrum *input = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
    if (!m_dirty && input == m_cached_input_ptr &&
        (!input || input->generation == m_cached_input_generation))
        return;
    m_dirty = false;
    m_cached_input_ptr = input;
    if (input)
        m_cached_input_generation = input->generation;

    auto &out = m_node.outputs[0];

    // Empty input -> empty output
    if (!input || input->frequencies.empty()) {
        out.frequencies.clear();
        out.tones.clear();
        out.noise_W.clear();
        out.noise_added_W.clear();
        out.noise_total_W.clear();
        out.phase_deg.clear();
        out.fs_Hz = m_fs_Hz / 2.0;
        out.is_complex_baseband = true;
        out.bumpGeneration();
        return;
    }

    // -- Compute output grid: N bins on [-Fs/4, Fs/4) --
    int N = 201;
    if (input->frequencies.size() >= 2) {
        double span = input->frequencies.back() - input->frequencies.front();
        double df_in = span / (input->frequencies.size() - 1);
        N = std::max(2, static_cast<int>(std::ceil((m_fs_Hz / 2.0) / df_in)));
    }
    double df_out = m_fs_Hz / (2.0 * N);
    out.frequencies.resize(N);
    for (int i = 0; i < N; ++i)
        out.frequencies[i] = -m_fs_Hz / 4.0 + i * df_out;
    out.fs_Hz = m_fs_Hz / 2.0;
    out.is_complex_baseband = true;

    // -- Noise mapping + NSD --
    double nsd_W_per_Hz = 0.001 * dbToLinear(m_nsd_dBm_per_Hz);
    out.noise_W.assign(N, 0.0);
    out.noise_added_W.assign(N, nsd_W_per_Hz);
    out.noise_total_W.resize(N, 0.0);
    out.phase_deg.assign(N, 0.0);

    for (int i = 0; i < N; ++i) {
        double f_out = out.frequencies[i];
        double f_a = f_out + m_fs_Hz / 4.0; // always in [0, Fs/2)

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

    // -- Tone mapping: alias -> NCO shift -> filter --
    out.tones.clear();
    for (const auto &tone : input->tones) {
        double f_a = alias_frequency(tone.freq_Hz, m_fs_Hz);
        double f_complex = f_a - m_fs_Hz / 4.0;
        if (f_complex < m_fs_Hz / 4.0) {
            Spectrum::Tone t = tone;
            t.freq_Hz = f_complex;
            out.tones.push_back(t);
        }
    }

    out.bumpGeneration();
}

nlohmann::json AdcEngine::serialize() const {
    return {{"sample_rate_Hz", m_fs_Hz}, {"nsd_dBm_per_Hz", m_nsd_dBm_per_Hz}};
}

void AdcEngine::deserialize(const nlohmann::json &j) {
    m_fs_Hz =
        j.contains("sample_rate_Hz") ? j["sample_rate_Hz"].get<double>() : j.value("fs_Hz", 1e9);
    m_nsd_dBm_per_Hz = j.value("nsd_dBm_per_Hz", -155.0);
    m_dirty = true;
}

std::string AdcEngine::hoverSummary() const {
    return "Fs: " + std::to_string(m_fs_Hz / 1e6) +
           " MHz | NSD: " + std::to_string(m_nsd_dBm_per_Hz) + " dBm/Hz";
}
