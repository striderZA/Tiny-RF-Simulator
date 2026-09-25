#pragma once

#include "component_interface.h"
#include "flow_result.h"
#include "flow_types.h"

#include <span>
#include <string>
#include <vector>

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

// Every way `spec` fails to apply to this live circuit, in the order RunFlow()
// checks them: each condition's component and its path/value compatibility,
// then each measurement's component, output port and metric. An empty result
// means the flow is runnable. Strictly a read of the circuit — nothing is
// written — so a caller may run it every frame.
//
// Exhaustive: every value of every condition is checked against the slot its
// path resolves to (resolveConditionSlot() in flow_params.h makes that one path
// parse per condition instead of one per value). RunFlow() runs this same pass
// before its first row, so a pre-flight built on it can never accept a flow the
// harness would refuse.
std::vector<FlowError> ValidateFlow(const FlowSpec &spec,
                                    std::span<IComponentEngine *const> components);

// Executes a loaded flow against a live circuit (see Task 5).
FlowResult RunFlow(const FlowSpec &spec, std::span<IComponentEngine *const> components,
                   NodeGraphEngine &graph);
