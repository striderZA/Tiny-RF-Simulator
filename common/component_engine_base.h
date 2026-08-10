#pragma once

#include "component_interface.h"
#include "node_graph_engine.h"
#include "signal_node.h"
#include <cstdint>
#include <string>
#include <string_view>

// ComponentEngineBase — shared boilerplate for the graph-attached DSP engines.
//
// Owns the members and accessors that every engine repeats verbatim:
//   - the component id and graph node id (id(), graphNodeId())
//   - the owning NodeGraphEngine pointer plus the graph node created by the
//     constructor via NodeGraphEngine::addNode (constructor shape: label +
//     id, input/output pin counts)
//   - the engine's SignalNode and the node() accessors
//   - the dirty flag that forces recomputation on the next update()
//   - the cached single-input (pointer, generation) pair with beginUpdate(),
//     the standard single-input dirty-check prologue
//
// Subclasses still implement the IComponentEngine pure virtuals
// (type_name(), hoverSummary(), update(), serialize(), deserialize()) and may
// override inputPinId()/outputPinId() for multi-pin layouts.
class ComponentEngineBase : public IComponentEngine {
  public:
    ComponentEngineBase(int id, NodeGraphEngine &graph, std::string_view label, int num_inputs,
                        int num_outputs)
        : m_id(id), m_graph(&graph) {
        m_graph_node_id = graph.addNode(std::string(label) + " " + std::to_string(id), &m_node,
                                        num_inputs, num_outputs);
        m_node.inputs.resize(num_inputs);
        m_node.outputs.resize(num_outputs);
    }

    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_node_id; }

    int inputPinId() const override { return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1; }
    int outputPinId() const override {
        return m_graph ? m_graph->outputPinId(m_graph_node_id) : -1;
    }

    SignalNode &node() override { return m_node; }
    const SignalNode &node() const override { return m_node; }

  protected:
    // Standard single-input dirty-check prologue. Returns true when update()
    // must recompute (dirty flag set, or the input pointer/generation changed
    // since the last update); on that path it clears the dirty flag and
    // records the current input for the next comparison. Returns false when
    // the input is unchanged and the engine is clean — callers return without
    // recomputing.
    bool beginUpdate(const Spectrum *in_ptr) {
        if (!m_dirty && in_ptr == m_cached_input_ptr &&
            (!in_ptr || in_ptr->generation == m_cached_input_generation))
            return false;
        m_dirty = false;
        m_cached_input_ptr = in_ptr;
        if (in_ptr)
            m_cached_input_generation = in_ptr->generation;
        return true;
    }

    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine *m_graph = nullptr;
    bool m_dirty = true;
    const Spectrum *m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;
    SignalNode m_node;
};
