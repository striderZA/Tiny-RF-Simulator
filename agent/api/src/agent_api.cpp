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
IComponentEngine *AgentApi::scratchTypeEngine(std::string_view type) const {
    const std::string key{type};
    if (const auto found = m_type_engines.find(key); found != m_type_engines.end())
        return found->second;
    const auto *descriptor = ComponentTypeRegistry::instance().find(type);
    if (!descriptor)
        return nullptr;
    IComponentEngine *engine =
        descriptor->create(m_type_components, m_type_graph, m_next_type_engine_id++);
    if (!engine)
        return nullptr;
    m_type_engines.emplace(key, engine);
    return engine;
}

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
        if (call.tool == "component_types") {
            result = executeComponentTypesTool(*this, call);
            summary = "List component types";
        } else if (call.tool == "library_search") {
            result = executeLibrarySearchTool(*this, call);
            summary = "Search component library";
        } else if (call.tool == "circuit_get") {
            result = executeCircuitReadTool(*this, call);
            summary = "Read circuit";
        } else if (call.tool == "component_get") {
            result = executeComponentReadTool(*this, call);
            summary = "Read component";
        } else if (call.tool == "circuit_edit") {
            result = executeCircuitEditTool(*this, call);
            summary = "Edit circuit";
        } else if (call.tool == "measure_port") {
            result = executeMeasurePortTool(*this, call);
            summary = "Measure output port";
        } else if (call.tool == "network_analyzer_sweep") {
            result = executeNetworkAnalyzerSweepTool(*this, call);
            summary = "Run network analyzer sweep";
        } else if (call.tool == "data_file_read") {
            result = executeDataFileReadTool(*this, call);
            summary = "Read data file";
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
    } catch (...) {
        LOG_ERROR("Agent tool %s failed: %s", call.tool.c_str(), "unknown exception");
    }
    return result;
}
