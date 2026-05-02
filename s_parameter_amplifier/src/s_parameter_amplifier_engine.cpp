#include "s_parameter_amplifier_engine.h"
#include "common.h"
#include "logging_core.h"
#include <cmath>

SParameterAmplifierEngine::SParameterAmplifierEngine(int id, NodeGraphEngine& graph,
                                                       const std::string& filepath)
    : m_id(id), m_graph(&graph), m_filepath(filepath) {
    m_graph_node_id = graph.addNode("S-Param Amp " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
    reload(filepath);
}

void SParameterAmplifierEngine::reload(const std::string& filepath) {
    m_filepath = filepath;
    m_forward_param_idx = 0;

    if (!m_data.load(filepath))
        return;

    int np = m_data.numPorts();
    m_forward_param_idx = (np > 1) ? np : 0;
    LOG_INFO("Loaded S-parameter amplifier %d from %s (%zu points, %d ports)",
             m_id, filepath.c_str(), m_data.freqs().size(), np);
}

int SParameterAmplifierEngine::inputPinId() const {
    return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1;
}

int SParameterAmplifierEngine::outputPinId() const {
    return m_graph ? m_graph->outputPinId(m_graph_node_id) : -1;
}

void SParameterAmplifierEngine::setForwardParamIdx(int idx) {
    int total = m_data.paramCount();
    if (idx >= 0 && idx < total)
        m_forward_param_idx = idx;
}

void SParameterAmplifierEngine::update(double dt) {
    (void)dt;
    auto& in = m_node.inputs[0];
    auto& out = m_node.outputs[0];

    m_data.applyToSpectrum(in, out, m_forward_param_idx);

    if (!m_data.loaded() || out.frequencies.empty())
        return;

    const size_t N = out.frequencies.size();
    if (N < 2)
        return;

    // Amplifier adds noise (filter does not)
    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i)
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
}

std::string SParameterAmplifierEngine::hoverSummary() const {
    if (!m_data.loaded()) return "Not loaded";
    int np = m_data.numPorts();
    return std::to_string(np) + "-port | Forward: S"
        + std::to_string((m_forward_param_idx / np) + 1)
        + std::to_string((m_forward_param_idx % np) + 1);
}