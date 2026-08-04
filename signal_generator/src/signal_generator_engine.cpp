#include "signal_generator_engine.h"
#include <nlohmann/json.hpp>

SignalGeneratorEngine::SignalGeneratorEngine(int id, NodeGraphEngine &graph)
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
    if (n < 2)
        n = 2;

    m_node.outputs.resize(1);
    m_node.outputs[0].frequencies.resize(n);
    for (int i = 0; i < n; ++i) {
        m_node.outputs[0].frequencies[i] = start_Hz + i * fixed_step;
    }

    m_node.outputs[0].noise_W.assign(n, 0.0);
    m_node.outputs[0].noise_added_W.assign(n, 0.0);
    m_node.outputs[0].phase_deg.assign(n, 0.0);
    m_node.outputs[0].computeTotalNoise();
}

void SignalGeneratorEngine::update(double) {
    if (!m_dirty)
        return;
    m_dirty = false;

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
        out.bumpGeneration();
        return;
    }

    // Unity gain for noise density (clean source)
    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        out.noise_W[i] = k * T;
    }

    // Generator adds no noise of its own
    out.noise_added_W.assign(N, 0.0);

    out.phase_deg.assign(N, 0.0);

    out.noise_total_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }

    out.fs_Hz = m_fs_Hz;
    out.is_complex_baseband = false;
    out.bumpGeneration();
}

nlohmann::json SignalGeneratorEngine::serialize() const {
    nlohmann::json tones = nlohmann::json::array();
    for (const auto &t : m_tones) {
        tones.push_back(
            {{"freq_Hz", t.freq_Hz}, {"power_dBm", t.power_dBm}, {"phase_deg", t.phase_deg}});
    }
    return {{"tones", tones}, {"fs_Hz", m_fs_Hz}};
}

void SignalGeneratorEngine::deserialize(const nlohmann::json &j) {
    m_tones.clear();
    if (j.contains("tones") && j["tones"].is_array()) {
        for (const auto &tj : j["tones"]) {
            m_tones.push_back({tj.value("freq_Hz", 100e6), tj.value("power_dBm", -20.0),
                               tj.value("phase_deg", 0.0)});
        }
    }
    m_fs_Hz = j.value("fs_Hz", 0.0);
    m_dirty = true;
}

std::string SignalGeneratorEngine::hoverSummary() const {
    if (m_tones.empty())
        return "0 tones";
    std::string s = std::to_string(m_tones.size()) + " tones: ";
    for (size_t i = 0; i < m_tones.size() && i < 3; ++i) {
        if (i > 0)
            s += ", ";
        s += std::to_string(m_tones[i].freq_Hz / 1e6) + " MHz @ " +
             std::to_string(m_tones[i].power_dBm) + " dBm";
    }
    if (m_tones.size() > 3)
        s += ", ...";
    return s;
}
