#include "ideal_filter_engine.h"
#include <cstdio>

IdealFilterEngine::IdealFilterEngine(int id, NodeGraphEngine& graph)
    : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("IdealFilter " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
}

int IdealFilterEngine::inputPinId() const {
    return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1;
}

int IdealFilterEngine::outputPinId() const {
    return m_graph ? m_graph->outputPinId(m_graph_node_id) : -1;
}

bool IdealFilterEngine::isInPassband(double freq_Hz) const {
    switch (m_type) {
        case FilterType::LPF: return freq_Hz <= m_fc_low_Hz;
        case FilterType::HPF: return freq_Hz > m_fc_low_Hz;
        case FilterType::BPF: return freq_Hz >= m_fc_low_Hz && freq_Hz <= m_fc_high_Hz;
        case FilterType::BSF: return freq_Hz < m_fc_low_Hz || freq_Hz > m_fc_high_Hz;
    }
    return true;
}

void IdealFilterEngine::update(double dt) {
    (void)dt;
    const Spectrum* in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
    if (!m_dirty && in_ptr == m_cached_input_ptr && (!in_ptr || in_ptr->generation == m_cached_input_generation))
        return;
    m_dirty = false;
    m_cached_input_ptr = in_ptr;
    if (in_ptr)
        m_cached_input_generation = in_ptr->generation;

    auto& out = m_node.outputs[0];

    if (in_ptr && !in_ptr->frequencies.empty()) {
        out.frequencies = in_ptr->frequencies;
    } else if (out.frequencies.size() < 2) {
        buildDefaultFrequencyGrid(out.frequencies);
    }

    const size_t N = out.frequencies.size();

    out.tones.clear();
    if (in_ptr) {
        for (const auto& t : in_ptr->tones) {
            if (isInPassband(t.freq_Hz))
                out.tones.push_back(t);
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

    out.noise_W.resize(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        if (isInPassband(out.frequencies[i]))
            out.noise_W[i] = (in_ptr && i < in_ptr->noise_W.size()) ? in_ptr->noise_W[i] : 0.0;
    }

    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i)
        out.noise_total_W[i] = out.noise_W[i];

    out.fs_Hz = in_ptr ? in_ptr->fs_Hz : 0.0;
    out.bumpGeneration();
}

std::string IdealFilterEngine::hoverSummary() const {
    const char* type_names[] = {"LPF", "HPF", "BPF", "BSF"};
    const char* tn = type_names[static_cast<int>(m_type)];
    char buf[128];
    if (m_type == FilterType::BPF || m_type == FilterType::BSF) {
        std::snprintf(buf, sizeof(buf), "Ideal %s | %.1f-%.1f MHz",
                      tn, m_fc_low_Hz / 1e6, m_fc_high_Hz / 1e6);
    } else {
        std::snprintf(buf, sizeof(buf), "Ideal %s | fc=%.1f MHz",
                      tn, m_fc_low_Hz / 1e6);
    }
    return buf;
}
