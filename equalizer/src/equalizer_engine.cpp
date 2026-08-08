#include "equalizer_engine.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <numbers>

EqualizerEngine::EqualizerEngine(int id, NodeGraphEngine &graph) : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("Equalizer " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
}

int EqualizerEngine::inputPinId() const {
    return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1;
}

int EqualizerEngine::outputPinId() const {
    return m_graph ? m_graph->outputPinId(m_graph_node_id) : -1;
}

void EqualizerEngine::setSParamFilepath(const std::string &path) {
    m_sparam_filepath = path;
    m_sparam_mode = m_sparam_data.load(path);
    if (m_sparam_data.loaded())
        m_sparam_fwd_idx = 1 * m_sparam_data.numPorts() + 0;
    m_dirty = true;
}

void EqualizerEngine::update(double dt) {
    (void)dt;
    const Spectrum *in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];

    // --- S-parameter mode ---
    if (m_sparam_mode && m_sparam_data.loaded()) {
        if (!m_dirty && in_ptr == m_cached_sparam_input &&
            (!in_ptr || in_ptr->generation == m_cached_sparam_generation))
            return;
        m_dirty = false;
        m_cached_sparam_input = in_ptr;
        if (in_ptr)
            m_cached_sparam_generation = in_ptr->generation;

        auto &out = m_node.outputs[0];

        if (in_ptr && !in_ptr->frequencies.empty())
            out.frequencies = in_ptr->frequencies;
        else if (out.frequencies.size() < 2)
            buildDefaultFrequencyGrid(out.frequencies);

        const size_t N = out.frequencies.size();
        int idx = m_sparam_fwd_idx;

        out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};
        out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
        for (auto &t : out.tones) {
            auto S = m_sparam_data.interpolate(t.freq_Hz, idx);
            t.power_dBm += 20.0 * std::log10(std::abs(S));
            t.phase_deg += std::arg(S) * 180.0 / std::numbers::pi;
        }

        if (in_ptr && !in_ptr->phase_deg.empty())
            out.phase_deg = in_ptr->phase_deg;
        else
            out.phase_deg.assign(N, 0.0);

        if (N < 2) {
            out.noise_W.assign(N, 0.0);
            out.noise_added_W.assign(N, 0.0);
            out.noise_total_W.assign(N, 0.0);
            out.bumpGeneration();
            return;
        }

        out.noise_W.assign(N, 0.0);
        for (size_t i = 0; i < N; ++i) {
            auto S = m_sparam_data.interpolate(out.frequencies[i], idx);
            double nin =
                (in_ptr && i < in_ptr->noise_total_W.size() ? in_ptr->noise_total_W[i] : 0.0);
            out.noise_W[i] = std::norm(S) * nin;
        }
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W = out.noise_W;
        out.fs_Hz = in_ptr ? in_ptr->fs_Hz : 0.0;
        out.bumpGeneration();
        return;
    }

    // --- Ideal mode ---
    if (!m_dirty && in_ptr == m_cached_input_ptr &&
        (!in_ptr || in_ptr->generation == m_cached_input_generation))
        return;
    m_dirty = false;
    m_cached_input_ptr = in_ptr;
    if (in_ptr)
        m_cached_input_generation = in_ptr->generation;

    auto &out = m_node.outputs[0];

    if (in_ptr && !in_ptr->frequencies.empty())
        out.frequencies = in_ptr->frequencies;
    else if (out.frequencies.size() < 2)
        buildDefaultFrequencyGrid(out.frequencies);

    const size_t N = out.frequencies.size();

    // Apply gain vs frequency profile
    out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};
    out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
    for (auto &t : out.tones) {
        double f = std::max(t.freq_Hz, 1.0);
        double ratio = std::max(f / m_ref_freq_Hz, 1e-30);
        double gain_db = m_ref_gain_dB + m_slope_dB_per_decade * std::log10(ratio);
        t.power_dBm += gain_db;
        // No phase rotation in ideal mode
    }

    if (in_ptr && !in_ptr->phase_deg.empty())
        out.phase_deg = in_ptr->phase_deg;
    else
        out.phase_deg.assign(N, 0.0);

    if (N < 2) {
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
        out.bumpGeneration();
        return;
    }

    // Apply gain to noise per bin
    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double f = std::max(out.frequencies[i], 1.0);
        double ratio = std::max(f / m_ref_freq_Hz, 1e-30);
        double gain_db = m_ref_gain_dB + m_slope_dB_per_decade * std::log10(ratio);
        double gain_linear = dbToLinear(gain_db);
        double nin = (in_ptr && i < in_ptr->noise_total_W.size() ? in_ptr->noise_total_W[i] : 0.0);
        out.noise_W[i] = gain_linear * nin;
    }
    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W = out.noise_W;
    out.fs_Hz = in_ptr ? in_ptr->fs_Hz : 0.0;
    out.bumpGeneration();
}

std::string EqualizerEngine::hoverSummary() const {
    if (m_sparam_mode && m_sparam_data.loaded()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "S-Param Equalizer | %zu pts",
                      m_sparam_data.freqs().size());
        return buf;
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Equalizer | Ref: %.1f dB @ %.0f MHz | Slope: %.1f dB/dec",
                  m_ref_gain_dB, m_ref_freq_Hz / 1e6, m_slope_dB_per_decade);
    return buf;
}

nlohmann::json EqualizerEngine::serialize() const {
    return {{"ref_gain_dB", m_ref_gain_dB},
            {"ref_freq_Hz", m_ref_freq_Hz},
            {"slope_dB_per_decade", m_slope_dB_per_decade},
            {"sparam_mode", m_sparam_mode},
            {"sparam_filepath", m_sparam_filepath},
            {"sparam_fwd_idx", m_sparam_fwd_idx}};
}

void EqualizerEngine::deserialize(const nlohmann::json &j) {
    m_ref_gain_dB = j.value("ref_gain_dB", 0.0);
    m_ref_freq_Hz = j.value("ref_freq_Hz", 1e9);
    m_slope_dB_per_decade = j.value("slope_dB_per_decade", 0.0);
    m_sparam_filepath = j.value("sparam_filepath", "");
    if (!m_sparam_filepath.empty())
        m_sparam_data.load(m_sparam_filepath);
    m_sparam_mode = j.value("sparam_mode", false) && m_sparam_data.loaded();
    m_sparam_fwd_idx = j.value("sparam_fwd_idx", 0);
    m_dirty = true;
}
