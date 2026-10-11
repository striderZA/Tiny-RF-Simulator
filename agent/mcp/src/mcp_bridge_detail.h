#pragma once

#include "agent_errors.h"

namespace mcp_bridge_detail {

inline AgentToolResult internalFailureResult() {
    return {true,
            {{"epoch", nullptr},
             {"error", {{"code", "INTERNAL"}, {"message", "Internal bridge error."}}}}};
}

} // namespace mcp_bridge_detail
