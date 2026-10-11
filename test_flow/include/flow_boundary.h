#pragma once

#include "component_interface.h"
#include "flow_result.h"
#include "flow_types.h"

#include <span>
#include <string>

class NodeGraphEngine;

// The run boundary shared by the Test Flow panel and the agent tool. It snapshots every
// engine, runs the flow, restores each snapshot independently, and rewires. A run leaves
// the circuit as it was unless a restore fails, which is reported as RestoreFailed.
enum class BoundaryStatus { Completed, SnapshotFailed, ExecutionFailed, RestoreFailed };

struct BoundaryOutcome {
    BoundaryStatus status = BoundaryStatus::Completed;
    FlowResult result;   // set when status == Completed
    std::string message; // snapshot, execution, or restore text; empty when Completed
};

BoundaryOutcome RunFlowWithinBoundary(const FlowSpec &spec,
                                      std::span<IComponentEngine *const> components,
                                      const NodeGraphEngine &graph);
