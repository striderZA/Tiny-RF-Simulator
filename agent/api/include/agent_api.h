#pragma once

#include "agent_errors.h"
#include "agent_host.h"
#include "circuit_runtime.h"
#include "component_library.h"
#include "component_registry.h"
#include "editor_commands.h"
#include "flow_boundary.h"
#include "measurement_chain_runner.h"
#include "network_analyzer_engine.h"
#include "node_graph_engine.h"
#include "receiver_performance_measurement.h"
#include "spectrum_analyzer_engine.h"
#include "view_manager.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class IAgentCallTask {
  public:
    virtual ~IAgentCallTask() = default;
    virtual std::optional<AgentToolResult> advance(std::chrono::steady_clock::duration budget) = 0;
    virtual void cancel() noexcept = 0;
    // Let callers preserve the terminal result when cancellation completes a request.
    // Existing tasks keep their cancellation behavior unless they need that detail.
    virtual void cancelWithResult(const AgentToolResult &) noexcept { cancel(); }
};

class IAgentCallExecutor {
  public:
    virtual ~IAgentCallExecutor() = default;

    virtual AgentToolResult execute(const AgentCall &) = 0;
    // Executors may return a task that yields across caller-thread pump iterations.
    // The default wraps execute() as a single-step task.
    virtual std::unique_ptr<IAgentCallTask> beginCall(const AgentCall &);
    virtual std::uint64_t epoch() const = 0;
};

struct AgentApiContext {
    EditorCommands &commands;
    CircuitRuntime &runtime;
    ComponentLibrary &library;
    NetworkAnalyzerEngine &network_analyzer;
    const IMeasurementChainHost &chain_host;
    const SpectrumAnalyzerEngine &spectrum_analyzer;
    IAgentHost &host;
};

// One-shot boundary for direct execution and tests. AgentServer calls use FlowRunSession; tests
// replace this seam to observe runs or force a restore failure.
using FlowRunBoundary = std::function<BoundaryOutcome(
    const FlowSpec &, std::span<IComponentEngine *const>, const NodeGraphEngine &)>;

class AgentApi final : public IAgentCallExecutor {
  public:
    explicit AgentApi(AgentApiContext context);

    AgentToolResult execute(const AgentCall &call) override;
    std::unique_ptr<IAgentCallTask> beginCall(const AgentCall &call) override;
    std::uint64_t epoch() const override;
    void noteProjectReplaced(AgentReplacementCause cause,
                             std::vector<std::string> undone_summaries = {});
    void setFlowRunBoundary(FlowRunBoundary boundary);

  private:
    friend AgentToolResult executeCircuitReadTool(const AgentApi &, const AgentCall &);
    friend AgentToolResult executeComponentReadTool(const AgentApi &, const AgentCall &);
    friend AgentToolResult executeComponentTypesTool(const AgentApi &, const AgentCall &);
    friend AgentToolResult executeLibrarySearchTool(const AgentApi &, const AgentCall &);
    friend AgentToolResult executeCircuitEditTool(const AgentApi &, const AgentCall &);
    friend AgentToolResult executeMeasurePortTool(const AgentApi &, const AgentCall &);
    friend AgentToolResult executeNetworkAnalyzerSweepTool(const AgentApi &, const AgentCall &);
    friend AgentToolResult executeDataFileReadTool(const AgentApi &, const AgentCall &);
    friend AgentToolResult executeTestFlowRunTool(AgentApi &, const AgentCall &);
    friend std::unique_ptr<IAgentCallTask> startTestFlowRunTask(AgentApi &, const AgentCall &);
    friend class AgentTestFlowRunTask;
    friend AgentToolResult executeReceiverMeasureTool(const AgentApi &, const AgentCall &);
    friend AgentToolResult executeProjectSaveTool(const AgentApi &, const AgentCall &);
    friend AgentToolResult executeProjectLoadTool(const AgentApi &, const AgentCall &);

    IComponentEngine *scratchTypeEngine(std::string_view type) const;

    mutable NodeGraphEngine m_type_graph;
    mutable ViewManager m_type_view;
    mutable ComponentRegistry m_type_components{m_type_graph, m_type_view};
    mutable std::unordered_map<std::string, IComponentEngine *> m_type_engines;

    mutable int m_next_type_engine_id = 1;
    AgentApiContext m_context;
    std::unique_ptr<ReceiverPerformanceMeasurementEngine> m_receiver_engine;
    std::string m_last_replacement_cause;
    std::vector<std::string> m_undone_summaries;
    FlowRunBoundary m_run_boundary = RunFlowWithinBoundary;
    // A custom boundary keeps beginCall() on the same synchronous test seam.
    bool m_custom_flow_boundary = false;
    // Set when a test-flow run could not restore the circuit; while set, runs are refused.
    std::string m_flow_latch_message;
};
