#include "amplifier_engine.h"

AmplifierEngine::AmplifierEngine(int id, NodeGraphEngine& graph)
    : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("Amplifier " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
}

int AmplifierEngine::inputPinId() const {
    if (!m_graph || m_graph_node_id < 0) return -1;
    for (const auto& node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            return node.input_pin_ids.empty() ? -1 : node.input_pin_ids[0];
        }
    }
    return -1;
}

int AmplifierEngine::outputPinId() const {
    if (!m_graph || m_graph_node_id < 0) return -1;
    for (const auto& node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            return node.output_pin_ids.empty() ? -1 : node.output_pin_ids[0];
        }
    }
    return -1;
}

void AmplifierEngine::update(double dt) {
    auto &in = m_node.inputs[0];
    auto &out = m_node.outputs[0];

    if (!in.frequencies.empty()) {
        out.frequencies = in.frequencies;
    } else if (out.frequencies.size() < 2) {
        const double start_Hz = MIN_FREQ;
        const double stop_Hz = MAX_FREQ;
        const double f_step_Hz = 10e6;
        int n = static_cast<int>((stop_Hz - start_Hz) / f_step_Hz);
        if (n < 2) {
            n = 2;
        }
        out.frequencies.resize(n);
        for (int i = 0; i < n; ++i) {
            out.frequencies[i] = start_Hz + i * f_step_Hz;
        }
    }

    out.tones = in.tones;
    for (auto &t : out.tones) {
        t.power_dBm += m_gain_dB;
    }

    const size_t N = out.frequencies.size();

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

    double G = dbToLinear(m_gain_dB);
    double added_density = addedNoiseDensity_W_per_Hz(m_nf_dB, G);

    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double nin = (i < in.noise_total_W.size() ? in.noise_total_W[i] : 0.0);
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
}
