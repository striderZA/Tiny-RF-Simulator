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

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class IAgentCallExecutor {
  public:
    virtual ~IAgentCallExecutor() = default;

    virtual AgentToolResult execute(const AgentCall &) = 0;
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

// Runs a flow over the circuit and returns its outcome. The default is RunFlowWithinBoundary;
// tests replace it to observe runs or to force a restore failure.
using FlowRunBoundary = std::function<BoundaryOutcome(
    const FlowSpec &, std::span<IComponentEngine *const>, const NodeGraphEngine &)>;

class AgentApi final : public IAgentCallExecutor {
  public:
    explicit AgentApi(AgentApiContext context);

    AgentToolResult execute(const AgentCall &call) override;
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
    // Set when a test-flow run could not restore the circuit; while set, runs are refused.
    std::string m_flow_latch_message;
};
