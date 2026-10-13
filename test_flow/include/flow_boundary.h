#pragma once

#include "component_interface.h"
#include "flow_result.h"
#include "flow_types.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <string>

class NodeGraphEngine;

// The one-shot boundary used by the Test Flow panel and direct AgentApi calls. It snapshots
// every engine, runs the flow, restores each snapshot independently, and rewires.
enum class BoundaryStatus { Completed, SnapshotFailed, ExecutionFailed, RestoreFailed };

struct BoundaryOutcome {
    BoundaryStatus status = BoundaryStatus::Completed;
    FlowResult result;   // set when status == Completed
    std::string message; // snapshot, execution, or restore text; empty when Completed
};

BoundaryOutcome RunFlowWithinBoundary(const FlowSpec &spec,
                                      std::span<IComponentEngine *const> components,
                                      const NodeGraphEngine &graph);

// A resumable version of the same boundary for callers that must yield between
// chunks. Every advance restores and rewires the original circuit before returning.
class FlowRunSession {
  public:
    FlowRunSession(const FlowSpec &spec, std::span<IComponentEngine *const> components,
                   const NodeGraphEngine &graph);
    ~FlowRunSession();

    FlowRunSession(const FlowRunSession &) = delete;
    FlowRunSession &operator=(const FlowRunSession &) = delete;

    // Runs up to max_rows, stopping after budget once at least one whole row has run.
    // A single row is indivisible and may take longer than budget.
    bool advance(std::size_t max_rows, std::chrono::steady_clock::duration budget);
    bool complete() const;
    const BoundaryOutcome &outcome() const;

    // Restore the captured state now, or discard the snapshot without touching live objects.
    void cancel();
    void abandon() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
