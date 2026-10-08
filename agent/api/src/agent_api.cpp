#include "agent_api.h"

#include "agent_args.h"
#include "agent_tools.h"
#include "logging_core.h"

#include <chrono>
#include <exception>
#include <string>
#include <utility>

namespace {

std::string errorCode(const AgentToolResult &result) {
    if (!result.is_error || !result.structured.contains("error") ||
        !result.structured["error"].contains("code"))
        return {};
    return result.structured["error"]["code"].get<std::string>();
}

} // namespace

AgentApi::AgentApi(AgentApiContext context) : m_context(context) {}

std::uint64_t AgentApi::epoch() const { return m_context.runtime.epoch(); }

void AgentApi::noteProjectReplaced(AgentReplacementCause cause,
                                   std::vector<std::string> undone_summaries) {
    switch (cause) {
    case AgentReplacementCause::NewProject:
        m_last_replacement_cause = "new_project";
        break;
    case AgentReplacementCause::OpenedProject:
        m_last_replacement_cause = "opened_project";
        break;
    case AgentReplacementCause::Tutorial:
        m_last_replacement_cause = "tutorial";
        break;
    case AgentReplacementCause::Reverted:
        m_last_replacement_cause = "reverted";
        break;
    }
    m_undone_summaries = std::move(undone_summaries);
}

AgentToolResult AgentApi::execute(const AgentCall &call) {
    const auto started = std::chrono::steady_clock::now();
    const auto activity_time = std::chrono::system_clock::now();
    AgentToolResult result;
    std::string summary;

    try {
        if (call.tool == "circuit_get") {
            result = executeCircuitReadTool(*this, call);
            summary = "Read circuit";
        } else if (call.tool == "component_get") {
            result = executeComponentReadTool(*this, call);
            summary = "Read component";
        } else {
            AgentError error{AgentErrorCode::InvalidArgument, "unknown tool " + call.tool};
            result = agentErrorResult(error, epoch());
            summary = error.message;
        }
    } catch (const AgentArgumentError &error) {
        result = agentErrorResult(error.error(), epoch());
        summary = error.what();
    } catch (const std::exception &error) {
        LOG_ERROR("Agent tool %s failed: %s", call.tool.c_str(), error.what());
        AgentError internal{AgentErrorCode::Internal,
                            "internal error in " + call.tool + "; see the RF Simulator log"};
        result = agentErrorResult(internal, epoch());
        summary = internal.message;
    } catch (...) {
        LOG_ERROR("Agent tool %s failed: %s", call.tool.c_str(), "unknown exception");
        AgentError internal{AgentErrorCode::Internal,
                            "internal error in " + call.tool + "; see the RF Simulator log"};
        result = agentErrorResult(internal, epoch());
        summary = internal.message;
    }

    AgentActivity activity;
    activity.time = activity_time;
    activity.tool = call.tool;
    activity.client = call.client;
    activity.summary = std::move(summary);
    activity.ok = !result.is_error;
    activity.error_code = errorCode(result);
    activity.duration_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
    try {
        m_context.host.recordActivity(activity);
    } catch (const std::exception &error) {
        LOG_ERROR("Agent tool %s failed: %s", call.tool.c_str(), error.what());
        AgentError internal{AgentErrorCode::Internal,
                            "internal error in " + call.tool + "; see the RF Simulator log"};
        return agentErrorResult(internal, epoch());
    } catch (...) {
        LOG_ERROR("Agent tool %s failed: %s", call.tool.c_str(), "unknown exception");
        AgentError internal{AgentErrorCode::Internal,
                            "internal error in " + call.tool + "; see the RF Simulator log"};
        return agentErrorResult(internal, epoch());
    }
    return result;
}
