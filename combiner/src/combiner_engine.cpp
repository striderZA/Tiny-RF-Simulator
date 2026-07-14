#include "combiner_engine.h"
#include "common.h"
#include <cmath>
#include <algorithm>
#include <numbers>

CombinerEngine::CombinerEngine(int id, NodeGraphEngine& graph)
    : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("Combiner " + std::to_string(id), &m_node, 2, 1);
    m_node.inputs.resize(2);
    m_node.outputs.resize(1);
}

int CombinerEngine::inputPinId(int port) const {
    if (!m_graph || m_graph_node_id < 0 || port < 0 || port >= 2) return -1;
    for (const auto& node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            if (static_cast<size_t>(port) >= node.input_pin_ids.size()) return -1;
            return node.input_pin_ids[port];
        }
    }
    return -1;
}

int CombinerEngine::outputPinId(int index) const {
    if (!m_graph || m_graph_node_id < 0 || index != 0) return -1;
    for (const auto& node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            if (node.output_pin_ids.empty()) return -1;
            return node.output_pin_ids[0];
        }
    }
    return -1;
}

void CombinerEngine::setManualMode(bool enabled) {
    m_manual_mode = enabled;
    m_dirty = true;
}

void CombinerEngine::setSParamMode(bool enabled) {
    m_sparam_mode = enabled;
    m_dirty = true;
}

void CombinerEngine::setSParamFile(const std::string& path) {
    m_sparam_path = path;
    m_sparam_mode = m_sparam.load(path);
    m_dirty = true;
}

std::string CombinerEngine::hoverSummary() const {
    return "Combiner: 2→1, -3 dB";
}

void CombinerEngine::update(double dt) {
    (void)dt;
    // Implementation in next task
}
