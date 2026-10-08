#pragma once

#include "agent_errors.h"
#include "agent_host.h"
#include "circuit_runtime.h"
#include "component_library.h"
#include "editor_commands.h"
#include "measurement_chain_runner.h"
#include "network_analyzer_engine.h"
#include "spectrum_analyzer_engine.h"

#include <cstdint>
#include <string>
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

class AgentApi final : public IAgentCallExecutor {
  public:
    explicit AgentApi(AgentApiContext context);

    AgentToolResult execute(const AgentCall &call) override;
    std::uint64_t epoch() const override;
    void noteProjectReplaced(AgentReplacementCause cause,
                             std::vector<std::string> undone_summaries = {});

  private:
    friend AgentToolResult executeCircuitReadTool(const AgentApi &, const AgentCall &);
    friend AgentToolResult executeComponentReadTool(const AgentApi &, const AgentCall &);

    AgentApiContext m_context;
    std::string m_last_replacement_cause;
    std::vector<std::string> m_undone_summaries;
};
