#include "signal_generator_engine.h"

SignalGeneratorEngine::SignalGeneratorEngine(int id, NodeGraphEngine& graph)
    : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("Generator " + std::to_string(id), &m_node, 0, 1);
    rebuildFrequencyGrid();
}

int SignalGeneratorEngine::outputPinId() const {
    return m_graph ? m_graph->outputPinId(m_graph_node_id) : -1;
}

void SignalGeneratorEngine::rebuildFrequencyGrid() {
    const double start_Hz = MIN_FREQ;
    const double stop_Hz = MAX_FREQ;
    constexpr double fixed_step = 10e6;
    int n = static_cast<int>((stop_Hz - start_Hz) / fixed_step);
    if (n < 2) n = 2;

    m_node.outputs.resize(1);
    m_node.inputs.resize(1);
    m_node.outputs[0].frequencies.resize(n);
    for (int i = 0; i < n; ++i) {
        m_node.outputs[0].frequencies[i] = start_Hz + i * fixed_step;
    }

    m_node.outputs[0].noise_W.assign(n, 0.0);
    m_node.outputs[0].noise_added_W.assign(n, 0.0);
    m_node.outputs[0].phase_deg.assign(n, 0.0);
    // Generator is an ideal source: flat thermal noise density k*T (W/Hz)
    m_node.inputs[0].noise_total_W.assign(n, k * T);
    m_node.inputs[0].phase_deg.assign(n, 0.0);
    m_node.outputs[0].computeTotalNoise();
}

void SignalGeneratorEngine::addTone(double freq_Hz, double power_dBm, double phase_deg) {
    m_tones.push_back({freq_Hz, power_dBm, phase_deg});
}

void SignalGeneratorEngine::removeTone(size_t index) {
    if (index < m_tones.size()) {
        m_tones.erase(m_tones.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

void SignalGeneratorEngine::updateTone(size_t index, double freq_Hz, double power_dBm, double phase_deg) {
    if (index < m_tones.size()) {
        m_tones[index].freq_Hz = freq_Hz;
        m_tones[index].power_dBm = power_dBm;
        m_tones[index].phase_deg = phase_deg;
    }
}

void SignalGeneratorEngine::update(double) {
    auto &in = m_node.inputs[0];
    auto &out = m_node.outputs[0];

    out.tones.clear();
    for (const auto &t : m_tones) {
        out.tones.push_back(t);
    }

    const size_t N = out.frequencies.size();
    if (N < 2) {
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
        out.phase_deg.assign(N, 0.0);
        return;
    }

    // Unity gain for noise density (clean source)
    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double nin = (i < in.noise_total_W.size() ? in.noise_total_W[i] : 0.0);
        out.noise_W[i] = nin;
    }

    // Generator adds no noise of its own
    out.noise_added_W.assign(N, 0.0);

    out.phase_deg.assign(N, 0.0);

    out.noise_total_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }
}
