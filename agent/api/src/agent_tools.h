#pragma once

#include "agent_api.h"

#include "component_params.h"

AgentToolResult executeCircuitReadTool(const AgentApi &api, const AgentCall &call);
AgentToolResult executeComponentReadTool(const AgentApi &api, const AgentCall &call);
AgentToolResult executeComponentTypesTool(const AgentApi &api, const AgentCall &call);
AgentToolResult executeLibrarySearchTool(const AgentApi &api, const AgentCall &call);
AgentToolResult executeCircuitEditTool(const AgentApi &api, const AgentCall &call);
AgentToolResult executeMeasurePortTool(const AgentApi &api, const AgentCall &call);
AgentToolResult executeNetworkAnalyzerSweepTool(const AgentApi &api, const AgentCall &call);
AgentToolResult executeDataFileReadTool(const AgentApi &api, const AgentCall &call);

AgentError agentParamError(const ParamWriteResult &result, int op_index);

IComponentEngine *findById(const CircuitRuntime &runtime, int component_id);
AgentToolResult staleMeasurementEpochError(std::uint64_t epoch, const std::string &cause,
                                           const std::vector<std::string> &undone);
