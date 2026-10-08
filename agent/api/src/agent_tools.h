#pragma once

#include "agent_api.h"

AgentToolResult executeCircuitReadTool(const AgentApi &api, const AgentCall &call);
AgentToolResult executeComponentReadTool(const AgentApi &api, const AgentCall &call);
