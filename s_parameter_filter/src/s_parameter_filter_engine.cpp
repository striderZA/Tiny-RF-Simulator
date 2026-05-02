#include "s_parameter_filter_engine.h"
#include "logging_core.h"

SParameterFilterEngine::SParameterFilterEngine(int id, NodeGraphEngine& graph,
                                                const std::string& filepath)
    : m_id(id), m_graph(&graph), m_filepath(filepath) {
    m_graph_node_id = graph.addNode("S-Param Filter " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
    reload(filepath);
}

void SParameterFilterEngine::reload(const std::string& filepath) {
    m_filepath = filepath;
    if (!m_data.load(filepath))
        return;
    LOG_INFO("Loaded S-parameter filter %d from %s (%zu points, %d ports)",
             m_id, filepath.c_str(), m_data.freqs().size(), m_data.numPorts());
}

int SParameterFilterEngine::inputPinId() const {
    return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1;
}

int SParameterFilterEngine::outputPinId() const {
    return m_graph ? m_graph->outputPinId(m_graph_node_id) : -1;
}

void SParameterFilterEngine::update(double dt) {
    (void)dt;
    auto& in = m_node.inputs[0];
    auto& out = m_node.outputs[0];

    int param_idx = (m_data.numPorts() > 1) ? m_data.numPorts() : 0; // S21 for 2-port
    m_data.applyToSpectrum(in, out, param_idx);
}

std::string SParameterFilterEngine::hoverSummary() const {
    if (!m_data.loaded()) return "Not loaded";
    return std::to_string(m_data.numPorts()) + "-port | "
        + std::to_string(m_data.freqs().size()) + " pts | Max: "
        + std::to_string(static_cast<int>(m_data.freqs().back() / 1e6)) + " MHz";
}