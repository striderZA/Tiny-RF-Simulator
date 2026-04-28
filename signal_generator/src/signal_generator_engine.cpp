#include "signal_generator_engine.h"

SignalGeneratorEngine::SignalGeneratorEngine(int id, NodeGraphEngine& graph)
    : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("Generator " + std::to_string(id), &m_node, false, true);
    rebuildFrequencyGrid();
}

int SignalGeneratorEngine::outputPinId() const {
    if (!m_graph || m_graph_node_id < 0) return -1;
    for (const auto& node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            return node.output_pin_id;
        }
    }
    return -1;
}

void SignalGeneratorEngine::rebuildFrequencyGrid() {
    const double start_Hz = MIN_FREQ;
    const double stop_Hz = MAX_FREQ;
    constexpr double fixed_step = 10e6;
    int n = static_cast<int>((stop_Hz - start_Hz) / fixed_step);
    if (n < 2) n = 2;

    m_node.output.frequencies.resize(n);
    for (int i = 0; i < n; ++i) {
        m_node.output.frequencies[i] = start_Hz + i * fixed_step;
    }

    m_node.output.noise_W.assign(n, 0.0);
    m_node.output.noise_added_W.assign(n, 0.0);
    // Generator is an ideal source: flat thermal noise density k*T (W/Hz)
    m_node.input.noise_total_W.assign(n, k * T);
    m_node.output.computeTotalNoise();
}

void SignalGeneratorEngine::addTone(double freq_Hz, double power_dBm) {
    m_tones.push_back({freq_Hz, power_dBm});
}

void SignalGeneratorEngine::removeTone(size_t index) {
    if (index < m_tones.size()) {
        m_tones.erase(m_tones.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

void SignalGeneratorEngine::updateTone(size_t index, double freq_Hz, double power_dBm) {
    if (index < m_tones.size()) {
        m_tones[index].freq_Hz = freq_Hz;
        m_tones[index].power_dBm = power_dBm;
    }
}

void SignalGeneratorEngine::update(double) {
    auto &in = m_node.input;
    auto &out = m_node.output;

    out.tones.clear();
    for (const auto &t : m_tones) {
        out.tones.push_back(t);
    }

    const size_t N = out.frequencies.size();
    if (N < 2) {
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
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

    out.noise_total_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }
}
