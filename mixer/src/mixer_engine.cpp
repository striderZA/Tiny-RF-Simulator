#include "mixer_engine.h"
#include <cmath>
#include <nlohmann/json.hpp>

MixerEngine::MixerEngine(int id, NodeGraphEngine &graph) : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("Mixer " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
}

int MixerEngine::inputPinId() const { return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1; }

int MixerEngine::outputPinId() const {
    return m_graph ? m_graph->outputPinId(m_graph_node_id) : -1;
}

void MixerEngine::update(double dt) {
    (void)dt;
    const Spectrum *in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
    if (!m_dirty && in_ptr == m_cached_input_ptr &&
        (!in_ptr || in_ptr->generation == m_cached_input_generation))
        return;
    m_dirty = false;
    m_cached_input_ptr = in_ptr;
    if (in_ptr)
        m_cached_input_generation = in_ptr->generation;

    auto &out = m_node.outputs[0];

    if (in_ptr && !in_ptr->frequencies.empty()) {
        out.frequencies = in_ptr->frequencies;
    } else if (out.frequencies.size() < 2) {
        buildDefaultFrequencyGrid(out.frequencies);
    }

    const size_t N = out.frequencies.size();

    // Frequency conversion: each input tone produces sum and difference
    out.tones.clear();
    out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
    if (in_ptr) {
        for (const auto &tone : in_ptr->tones) {
            Spectrum::Tone lower;
            lower.freq_Hz = std::abs(tone.freq_Hz - m_lo_freq_Hz);
            lower.power_dBm = tone.power_dBm + m_conv_gain_dB;
            lower.phase_deg = tone.phase_deg;
            out.tones.push_back(lower);

            Spectrum::Tone upper;
            upper.freq_Hz = tone.freq_Hz + m_lo_freq_Hz;
            upper.power_dBm = tone.power_dBm + m_conv_gain_dB;
            upper.phase_deg = tone.phase_deg;
            out.tones.push_back(upper);
        }
    }

    if (in_ptr && !in_ptr->phase_deg.empty()) {
        out.phase_deg = in_ptr->phase_deg;
    } else {
        out.phase_deg.assign(N, 0.0);
    }

    if (N < 2) {
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
        out.phase_deg.assign(N, 0.0);
        out.bumpGeneration();
        return;
    }

    double G = dbToLinear(m_conv_gain_dB);
    double added_density = addedNoiseDensity_W_per_Hz(m_nf_dB, G);

    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double nin = (in_ptr && i < in_ptr->noise_total_W.size() ? in_ptr->noise_total_W[i] : 0.0);
        out.noise_W[i] = G * nin;
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

    out.bumpGeneration();
}

nlohmann::json MixerEngine::serialize() const {
    return {{"lo_freq_Hz", m_lo_freq_Hz}, {"conv_gain_dB", m_conv_gain_dB}, {"nf_dB", m_nf_dB}};
}

void MixerEngine::deserialize(const nlohmann::json &j) {
    m_lo_freq_Hz = j.value("lo_freq_Hz", 1e9);
    m_conv_gain_dB = j.contains("conv_gain_dB") ? j["conv_gain_dB"].get<double>()
                                                : j.value("conversion_gain_dB", -6.0);
    m_nf_dB = j.value("nf_dB", 0.0);
    m_dirty = true;
}

std::string MixerEngine::hoverSummary() const {
    return "LO: " + std::to_string(m_lo_freq_Hz / 1e6) +
           " MHz | Conv Gain: " + std::to_string(m_conv_gain_dB) +
           " dB | NF: " + std::to_string(m_nf_dB) + " dB";
}
