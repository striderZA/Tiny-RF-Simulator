#include "adc_engine.h"

AdcEngine::AdcEngine(int id, NodeGraphEngine& graph)
    : m_id(id)
    , m_graph(&graph)
    , m_node(SignalNode{ {}, {}, false })
{
    m_graph_node_id = m_graph->addNode("ADC " + std::to_string(id), &m_node, 1, 1);
}

void AdcEngine::update(double dt)
{
    (void)dt;
}

int AdcEngine::inputPinId() const
{
    for (auto& n : m_graph->nodes()) {
        if (n.node_id == m_graph_node_id && !n.input_pin_ids.empty())
            return n.input_pin_ids[0];
    }
    return -1;
}

int AdcEngine::outputPinId() const
{
    for (auto& n : m_graph->nodes()) {
        if (n.node_id == m_graph_node_id && !n.output_pin_ids.empty())
            return n.output_pin_ids[0];
    }
    return -1;
}
