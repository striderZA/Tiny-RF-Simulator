#include "splitter_engine.h"
#include <nlohmann/json.hpp>

SplitterEngine::SplitterEngine(int id, NodeGraphEngine &graph) : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("Splitter " + std::to_string(id), &m_node, 1, 2);
    m_node.inputs.resize(1);
    m_node.outputs.resize(2);
}

int SplitterEngine::inputPinId() const {
    return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1;
}

int SplitterEngine::outputPinId(int index) const {
    if (!m_graph || m_graph_node_id < 0)
        return -1;
    for (const auto &node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            if (index < 0 || static_cast<size_t>(index) >= node.output_pin_ids.size())
                return -1;
            return node.output_pin_ids[index];
        }
    }
    return -1;
}

void SplitterEngine::update(double dt) {
    (void)dt;
    const Spectrum *in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
    if (!m_dirty && in_ptr == m_cached_input_ptr &&
        (!in_ptr || in_ptr->generation == m_cached_input_generation))
        return;
    m_dirty = false;
    m_cached_input_ptr = in_ptr;
    if (in_ptr)
        m_cached_input_generation = in_ptr->generation;

    for (size_t out_idx = 0; out_idx < m_node.outputs.size(); ++out_idx) {
        auto &out = m_node.outputs[out_idx];

        if (in_ptr && !in_ptr->frequencies.empty()) {
            out.frequencies = in_ptr->frequencies;
        } else if (out.frequencies.size() < 2) {
            buildDefaultFrequencyGrid(out.frequencies);
        }

        const size_t N = out.frequencies.size();

        out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};
        out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
        out.fs_Hz = in_ptr ? in_ptr->fs_Hz : 0.0;
        for (auto &t : out.tones) {
            t.power_dBm -= SPLIT_LOSS_DB;
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
            continue;
        }

        double loss_linear = dbToLinear(-SPLIT_LOSS_DB);

        out.noise_W.assign(N, 0.0);
        for (size_t i = 0; i < N; ++i) {
            double nin =
                (in_ptr && i < in_ptr->noise_total_W.size() ? in_ptr->noise_total_W[i] : 0.0);
            out.noise_W[i] = loss_linear * nin;
        }

        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.resize(N);
        for (size_t i = 0; i < N; ++i) {
            out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
        }
    }

    for (auto &out : m_node.outputs)
        out.bumpGeneration();
}

nlohmann::json SplitterEngine::serialize() const { return nlohmann::json::object(); }

void SplitterEngine::deserialize(const nlohmann::json &) { m_dirty = true; }

std::string SplitterEngine::hoverSummary() const { return "Split: -3 dB | 1 in, 2 out"; }
