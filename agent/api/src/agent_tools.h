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
AgentToolResult executeTestFlowRunTool(AgentApi &api, const AgentCall &call);
AgentToolResult executeReceiverMeasureTool(const AgentApi &api, const AgentCall &call);

AgentError agentParamError(const ParamWriteResult &result, int op_index);

IComponentEngine *findById(const CircuitRuntime &runtime, int component_id);
AgentToolResult staleMeasurementEpochError(std::uint64_t epoch, const std::string &cause,
                                           const std::vector<std::string> &undone);

// A measured output endpoint: a live component, its output port index, and its graph pin.
struct AgentEndpoint {
    IComponentEngine *component = nullptr;
    int port = -1;
    int pin = -1;
};
AgentEndpoint resolveOutputEndpoint(const AgentApiContext &context,
                                    const nlohmann::ordered_json &value, std::string_view path);
