#pragma once

#include "agent_api.h"

AgentToolResult executeCircuitReadTool(const AgentApi &api, const AgentCall &call);
AgentToolResult executeComponentReadTool(const AgentApi &api, const AgentCall &call);
AgentToolResult executeComponentTypesTool(const AgentApi &api, const AgentCall &call);
AgentToolResult executeLibrarySearchTool(const AgentApi &api, const AgentCall &call);
