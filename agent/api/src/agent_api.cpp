#include "agent_api.h"

#include "agent_args.h"
#include "agent_tools.h"
#include "logging_core.h"

#include <chrono>
#include <exception>
#include <memory>
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

namespace {

class CompletedAgentCallTask final : public IAgentCallTask {
  public:
    explicit CompletedAgentCallTask(AgentToolResult result) : m_result(std::move(result)) {}

    std::optional<AgentToolResult> advance(std::chrono::steady_clock::duration) override {
        if (!m_result)
            return std::nullopt;
        std::optional<AgentToolResult> result{std::move(*m_result)};
        m_result.reset();
        return result;
    }

    void cancel() noexcept override { m_result.reset(); }

  private:
    std::optional<AgentToolResult> m_result;
};

class ActivityAgentCallTask final : public IAgentCallTask {
  public:
    ActivityAgentCallTask(std::unique_ptr<IAgentCallTask> task,
                          std::function<void(const AgentToolResult &)> record_activity,
                          std::function<AgentToolResult()> cancellation_result)
        : m_task(std::move(task)), m_record_activity(std::move(record_activity)),
          m_cancellation_result(std::move(cancellation_result)) {}

    ~ActivityAgentCallTask() override { cancel(); }

    std::optional<AgentToolResult> advance(std::chrono::steady_clock::duration budget) override {
        if (m_complete)
            return std::nullopt;
        auto result = m_task->advance(budget);
        if (result) {
            m_complete = true;
            m_record_activity(*result);
        }
        return result;
    }

    void cancel() noexcept override {
        if (m_complete)
            return;
        m_task->cancel();
        m_complete = true;
        try {
            const AgentToolResult result = m_cancellation_result();
            recordActivity(result);
        } catch (...) {
            // Activity reporting must not unwind AgentServer shutdown.
        }
    }

    void cancelWithResult(const AgentToolResult &result) noexcept override {
        if (m_complete)
            return;
        m_task->cancel();
        m_complete = true;
        recordActivity(result);
    }

  private:
    void recordActivity(const AgentToolResult &result) noexcept {
        try {
            m_record_activity(result);
        } catch (...) {
            // Activity reporting must not unwind AgentServer shutdown.
        }
    }

    std::unique_ptr<IAgentCallTask> m_task;
    std::function<void(const AgentToolResult &)> m_record_activity;
    std::function<AgentToolResult()> m_cancellation_result;
    bool m_complete = false;
};

} // namespace

std::unique_ptr<IAgentCallTask> IAgentCallExecutor::beginCall(const AgentCall &call) {
    return std::make_unique<CompletedAgentCallTask>(execute(call));
}

AgentApi::AgentApi(AgentApiContext context)
    : m_context(context), m_receiver_engine(std::make_unique<ReceiverPerformanceMeasurementEngine>(
                              context.runtime.graph(), context.chain_host)) {}

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
    m_flow_latch_message.clear();
}

void AgentApi::setFlowRunBoundary(FlowRunBoundary boundary) {
    m_run_boundary = std::move(boundary);
    m_custom_flow_boundary = true;
}

std::unique_ptr<IAgentCallTask> AgentApi::beginCall(const AgentCall &call) {
    if (call.tool != "test_flow_run" || m_custom_flow_boundary)
        return IAgentCallExecutor::beginCall(call);

    const auto started = std::chrono::steady_clock::now();
    const auto activity_time = std::chrono::system_clock::now();
    std::unique_ptr<IAgentCallTask> task;
    try {
        task = startTestFlowRunTask(*this, call);
    } catch (const AgentArgumentError &error) {
        task = std::make_unique<CompletedAgentCallTask>(agentErrorResult(error.error(), epoch()));
    } catch (const std::exception &error) {
        LOG_ERROR("Agent tool %s failed: %s", call.tool.c_str(), error.what());
        task = std::make_unique<CompletedAgentCallTask>(
            agentErrorResult(AgentError{AgentErrorCode::Internal, "internal error in " + call.tool +
                                                                      "; see the RF Simulator log"},
                             epoch()));
    } catch (...) {
        LOG_ERROR("Agent tool %s failed: %s", call.tool.c_str(), "unknown exception");
        task = std::make_unique<CompletedAgentCallTask>(
            agentErrorResult(AgentError{AgentErrorCode::Internal, "internal error in " + call.tool +
                                                                      "; see the RF Simulator log"},
                             epoch()));
    }

    const std::string client = call.client;
    auto record_activity = [this, client, started, activity_time](const AgentToolResult &result) {
        AgentActivity activity;
        activity.time = activity_time;
        activity.tool = "test_flow_run";
        activity.client = client;
        activity.summary = "Run test flow";
        activity.ok = !result.is_error;
        activity.error_code = errorCode(result);
        activity.duration_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();
        try {
            m_context.host.recordActivity(activity);
        } catch (const std::exception &error) {
            LOG_ERROR("Agent tool %s failed: %s", "test_flow_run", error.what());
        } catch (...) {
            LOG_ERROR("Agent tool %s failed: %s", "test_flow_run", "unknown exception");
        }
    };
    auto cancellation_result = [this] {
        return agentErrorResult(AgentError{AgentErrorCode::Busy, "agent call cancelled"}, epoch());
    };
    return std::make_unique<ActivityAgentCallTask>(std::move(task), std::move(record_activity),
                                                   std::move(cancellation_result));
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
        } else if (call.tool == "library_create") {
            result = executeLibraryCreateTool(*this, call);
            summary = "Create component-library entry";
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
        } else if (call.tool == "test_flow_run") {
            result = executeTestFlowRunTool(*this, call);
            summary = "Run test flow";
        } else if (call.tool == "receiver_measure") {
            result = executeReceiverMeasureTool(*this, call);
            summary = "Receiver measure";
        } else if (call.tool == "project_save") {
            result = executeProjectSaveTool(*this, call);
            summary = "Save project";
        } else if (call.tool == "project_load") {
            result = executeProjectLoadTool(*this, call);
            summary = "Load project";
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
