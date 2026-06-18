#include "coax_cable_engine.h"
#include "logging_core.h"
#include <algorithm>
#include <cmath>

CoaxCableEngine::CoaxCableEngine(int id, NodeGraphEngine& graph)
    : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("Coax Cable " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
}

int CoaxCableEngine::inputPinId() const {
    return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1;
}

int CoaxCableEngine::outputPinId() const {
    return m_graph ? m_graph->outputPinId(m_graph_node_id) : -1;
}

void CoaxCableEngine::setPresetIndex(int idx) {
    if (idx < 0 || static_cast<size_t>(idx) >= kCoaxCablePresets.size()) return;
    if (idx != m_preset_index) {
        m_preset_index = idx;
        m_dirty = true;
        m_warned_above_max = false;  // reset warn flag on preset change
    }
}

void CoaxCableEngine::setLengthM(double m) {
    double clamped = std::clamp(m, 0.0, 1000.0);
    if (clamped != m_length_m) {
        m_length_m = clamped;
        m_dirty = true;
    }
}

void CoaxCableEngine::setConnectorsLossDB(double db) {
    if (db != m_connectors_loss_dB) {
        m_connectors_loss_dB = db;
        m_dirty = true;
    }
}

void CoaxCableEngine::update(double dt) {
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

    // Pass-through: copy frequencies, tones, phase, noise from input. The
    // frequency-dependent loss and phase shift are added in Task 3+.
    if (in_ptr && !in_ptr->frequencies.empty()) {
        out.frequencies = in_ptr->frequencies;
    } else if (out.frequencies.size() < 2) {
        buildDefaultFrequencyGrid(out.frequencies);
    }

    out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};

    const CableSpec& p = preset();
    const double max_f_Hz = p.max_freq_GHz * 1e9;
    for (auto& t : out.tones) {
        const double f_Hz_raw = std::abs(t.freq_Hz);
        const double f_Hz = std::clamp(f_Hz_raw, 1.0, max_f_Hz);
        if (f_Hz_raw > max_f_Hz && !m_warned_above_max) {
            LOG_WARN("Coax cable %d: tone at %.3e Hz exceeds preset %s max freq (%.3e Hz); clamping.",
                     m_id, f_Hz_raw, p.name, max_f_Hz);
            m_warned_above_max = true;
        }
        const double f_MHz = f_Hz / 1e6;
        const double loss_dB =
            (p.K1_dB_per_m * std::sqrt(f_MHz) + p.K2_dB_per_m * f_MHz) * m_length_m
            + m_connectors_loss_dB;
        t.power_dBm -= loss_dB;
    }

    const size_t N = out.frequencies.size();
    if (in_ptr && !in_ptr->phase_deg.empty()) {
        out.phase_deg = in_ptr->phase_deg;
    } else {
        out.phase_deg.assign(N, 0.0);
    }

    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double nin = (in_ptr && i < in_ptr->noise_total_W.size()) ? in_ptr->noise_total_W[i] : 0.0;
        out.noise_W[i] = nin;  // pass-through; scaling added in Task 4
    }
    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i) {
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }

    out.bumpGeneration();
}

std::string CoaxCableEngine::hoverSummary() const {
    return std::string("Coax Cable: ") + preset().name +
           " | L=" + std::to_string(m_length_m) + " m";
}
