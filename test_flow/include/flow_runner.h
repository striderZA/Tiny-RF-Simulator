#pragma once

#include "component_interface.h"
#include "flow_result.h"
#include "flow_types.h"

#include <span>
#include <string>

class NodeGraphEngine;

struct FlowLoadResult {
    bool ok = false;
    FlowError error;
    FlowSpec spec;
};

// Parses and validates a flow file: version, section shapes, field types,
// duplicate (component, path) targets, non-empty value lists, and known metric
// names. Never touches a circuit.
FlowLoadResult LoadFlowFile(const std::string &path);

// Executes a loaded flow against a live circuit (see Task 5).
FlowResult RunFlow(const FlowSpec &spec, std::span<IComponentEngine *const> components,
                   NodeGraphEngine &graph);
