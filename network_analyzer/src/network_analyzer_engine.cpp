#include "network_analyzer_engine.h"
#include "common.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>

NetworkAnalyzerEngine::NetworkAnalyzerEngine(int id, NodeGraphEngine &graph)
    : ComponentEngineBase(id, graph, "Network Analyzer", 1, 1) {
    graph.setNodePinLabels(m_graph_node_id, {"MEAS"}, {"STIM"});
}

void NetworkAnalyzerEngine::setStartFrequency(double hz) {
    if (hz != m_start_freq) {
        m_start_freq = hz;
        m_sweep_params_dirty = true;
    }
}

void NetworkAnalyzerEngine::setStopFrequency(double hz) {
    if (hz != m_stop_freq) {
        m_stop_freq = hz;
        m_sweep_params_dirty = true;
    }
}

void NetworkAnalyzerEngine::setPoints(int n) {
    int clamped = std::clamp(n, 2, 2001);
    if (clamped != m_points) {
        m_points = clamped;
        m_sweep_params_dirty = true;
    }
}

void NetworkAnalyzerEngine::setStimulusPower(double dBm) {
    if (dBm != m_stimulus_power_dBm) {
        m_stimulus_power_dBm = dBm;
        m_sweep_params_dirty = true;
    }
}

void NetworkAnalyzerEngine::rebuildStimulus() {
    const int n = m_points;
    m_stimulus_freqs.resize(static_cast<size_t>(n));
    const double span = m_stop_freq - m_start_freq;
    for (int i = 0; i < n; ++i) {
        double t = (n > 1) ? static_cast<double>(i) / (n - 1) : 0.0;
        m_stimulus_freqs[static_cast<size_t>(i)] = m_start_freq + span * t;
    }

    auto &out = m_node.outputs[0];
    out.frequencies = m_stimulus_freqs;
    out.tones.resize(m_stimulus_freqs.size());
    for (size_t i = 0; i < m_stimulus_freqs.size(); ++i)
        out.tones[i] = {m_stimulus_freqs[i], m_stimulus_power_dBm, 0.0};

    out.noise_W.assign(m_stimulus_freqs.size(), k * T);
    out.noise_added_W.assign(m_stimulus_freqs.size(), 0.0);
    out.phase_deg.assign(m_stimulus_freqs.size(), 0.0);
    out.computeTotalNoise();
    out.fs_Hz = 0.0;
    out.is_complex_baseband = false;
    out.bumpGeneration();

    m_gain_dB.assign(m_stimulus_freqs.size(), std::numeric_limits<double>::quiet_NaN());
    m_nf_dB.assign(m_stimulus_freqs.size(), std::numeric_limits<double>::quiet_NaN());
}

void NetworkAnalyzerEngine::computeMeasurement(const Spectrum *response) {
    ++m_measurement_recompute_count;
    const size_t N = m_stimulus_freqs.size();
    m_gain_dB.assign(N, std::numeric_limits<double>::quiet_NaN());
    m_nf_dB.assign(N, std::numeric_limits<double>::quiet_NaN());
    if (!response)
        return;

    constexpr double kFreqEpsilonHz = 1.0;
    for (size_t i = 0; i < N; ++i) {
        const double f = m_stimulus_freqs[i];

        const Spectrum::Tone *matched_tone = nullptr;
        for (const auto &t : response->tones) {
            if (std::abs(t.freq_Hz - f) <= kFreqEpsilonHz) {
                matched_tone = &t;
                break;
            }
        }
        if (!matched_tone)
            continue;

        const double gain_dB = matched_tone->power_dBm - m_stimulus_power_dBm;
        if (gain_dB < -100.0)
            continue; // indistinguishable from noise floor -> no data

        size_t noise_idx = response->frequencies.size();
        for (size_t j = 0; j < response->frequencies.size(); ++j) {
            if (std::abs(response->frequencies[j] - f) <= kFreqEpsilonHz) {
                noise_idx = j;
                break;
            }
        }

        m_gain_dB[i] = gain_dB;

        if (noise_idx < response->noise_total_W.size()) {
            const double gain_linear = dbToLinear(gain_dB);
            const double noise_out_W = response->noise_total_W[noise_idx];
            const double nf_linear = (noise_out_W / gain_linear) / (k * T);
            if (nf_linear > 0.0)
                m_nf_dB[i] = 10.0 * std::log10(nf_linear);
        }
    }
}

void NetworkAnalyzerEngine::update(double dt) {
    (void)dt;
    if (m_sweep_params_dirty) {
        rebuildStimulus();
        m_sweep_params_dirty = false;
        m_dirty = true; // force re-measurement below even if Port 2 unchanged
    }

    const Spectrum *in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
    if (!m_dirty && in_ptr == m_cached_input_ptr &&
        (!in_ptr || in_ptr->generation == m_cached_input_generation))
        return;
    m_dirty = false;
    m_cached_input_ptr = in_ptr;
    m_cached_input_generation = in_ptr ? in_ptr->generation : 0;

    computeMeasurement(in_ptr);
}

nlohmann::json NetworkAnalyzerEngine::serialize() const {
    return {{"start_freq_hz", m_start_freq},
            {"stop_freq_hz", m_stop_freq},
            {"points", m_points},
            {"stimulus_power_dBm", m_stimulus_power_dBm}};
}

void NetworkAnalyzerEngine::deserialize(const nlohmann::json &j) {
    m_start_freq = j.value("start_freq_hz", 1e9);
    m_stop_freq = j.value("stop_freq_hz", 6e9);
    m_points = std::clamp(j.value("points", 201), 2, 2001);
    m_stimulus_power_dBm = j.value("stimulus_power_dBm", -30.0);
    m_sweep_params_dirty = true;
}

std::string NetworkAnalyzerEngine::hoverSummary() const {
    return "Network Analyzer: " + std::to_string(static_cast<int>(m_start_freq / 1e6)) + "-" +
           std::to_string(static_cast<int>(m_stop_freq / 1e6)) + " MHz, " +
           std::to_string(m_points) + " pts";
}
