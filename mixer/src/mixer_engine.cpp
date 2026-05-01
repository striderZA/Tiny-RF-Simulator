#include "mixer_engine.h"
#include <cmath>

MixerEngine::MixerEngine(int id, NodeGraphEngine& graph)
    : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("Mixer " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
}

int MixerEngine::inputPinId() const {
    return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1;
}

int MixerEngine::outputPinId() const {
    return m_graph ? m_graph->outputPinId(m_graph_node_id) : -1;
}

void MixerEngine::update(double dt) {
    (void)dt;
    auto& in = m_node.inputs[0];
    auto& out = m_node.outputs[0];

    if (!in.frequencies.empty()) {
        out.frequencies = in.frequencies;
    } else if (out.frequencies.size() < 2) {
        buildDefaultFrequencyGrid(out.frequencies);
    }

    const size_t N = out.frequencies.size();

    // Frequency conversion: each input tone produces sum and difference
    out.tones.clear();
    for (const auto& tone : in.tones) {
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

    if (!in.phase_deg.empty()) {
        out.phase_deg = in.phase_deg;
    } else {
        out.phase_deg.assign(N, 0.0);
    }

    if (N < 2) {
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
        out.phase_deg.assign(N, 0.0);
        return;
    }

    double G = dbToLinear(m_conv_gain_dB);

    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double nin = (i < in.noise_total_W.size() ? in.noise_total_W[i] : 0.0);
        out.noise_W[i] = G * nin;
    }

    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i) {
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }
}

std::string MixerEngine::hoverSummary() const {
    return "LO: " + std::to_string(m_lo_freq_Hz / 1e6) + " MHz | Conv Gain: " + std::to_string(m_conv_gain_dB) + " dB";
}
