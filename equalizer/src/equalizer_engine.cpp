#include "equalizer_engine.h"
#include <cmath>
#include <cstdio>

EqualizerEngine::EqualizerEngine(int id, NodeGraphEngine& graph)
    : m_id(id), m_graph(&graph) {
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

void EqualizerEngine::update(double dt) {
    (void)dt;
    const Spectrum* in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
    if (!m_dirty && in_ptr == m_cached_input_ptr &&
        (!in_ptr || in_ptr->generation == m_cached_input_generation)) {
        return;
    }
    m_dirty = false;
    m_cached_input_ptr = in_ptr;
    if (in_ptr) m_cached_input_generation = in_ptr->generation;

    auto& out = m_node.outputs[0];

    if (in_ptr && !in_ptr->frequencies.empty()) {
        out.frequencies = in_ptr->frequencies;
    } else if (out.frequencies.size() < 2) {
        buildDefaultFrequencyGrid(out.frequencies);
    }

    out.tones.clear();
    if (in_ptr) {
        for (const auto& t : in_ptr->tones) {
            Spectrum::Tone out_t = t;
            out_t.power_dBm -= lossAt(t.freq_Hz);
            out.tones.push_back(out_t);
        }
    }

    const size_t N = out.frequencies.size();
    if (in_ptr && !in_ptr->phase_deg.empty()) {
        out.phase_deg = in_ptr->phase_deg;
    } else {
        out.phase_deg.assign(N, 0.0);
    }

    out.noise_W.assign(N, 0.0);
    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i) {
        const double nin = (in_ptr && i < in_ptr->noise_total_W.size()) ? in_ptr->noise_total_W[i] : 0.0;
        const double L_lin = dbToLinear(lossAt(out.frequencies[i]));
        out.noise_W[i] = nin / L_lin;
        out.noise_total_W[i] = out.noise_W[i];
    }

    out.fs_Hz = in_ptr ? in_ptr->fs_Hz : 0.0;
    out.bumpGeneration();
}

std::string EqualizerEngine::hoverSummary() const {
    return "Equalizer";
}

void EqualizerEngine::setLossAtDC(double dB) {
    if (dB != m_loss_at_DC_dB) {
        m_loss_at_DC_dB = dB;
        m_dirty = true;
    }
}

void EqualizerEngine::setSlope(double dB_per_decade) {
    if (dB_per_decade != m_slope_dB_per_decade) {
        m_slope_dB_per_decade = dB_per_decade;
        m_dirty = true;
    }
}

double EqualizerEngine::lossAt(double freq_Hz) const {
    double f = std::abs(freq_Hz);
    if (f < 1.0) f = 1.0;
    return m_loss_at_DC_dB + m_slope_dB_per_decade * std::log10(f);
}
