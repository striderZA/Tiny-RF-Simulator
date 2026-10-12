#include "flow_boundary.h"

#include "flow_runner.h"
#include "rewire.h"

#include <cstdint>
#include <exception>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Snapshot {
    int id = 0;
    nlohmann::json state;
};

// Restore messages join with "; " in the order the restores ran.
void appendMessage(std::string &messages, const std::string &message) {
    if (!messages.empty())
        messages += "; ";
    messages += message;
}

std::string componentLabel(int id) { return "component " + std::to_string(id) + ": "; }
bool captureSnapshots(std::span<IComponentEngine *const> components,
                      std::vector<Snapshot> &snapshots, std::string &failure) {
    snapshots.reserve(components.size());
    for (IComponentEngine *component : components) {
        if (!component)
            continue;
        try {
            snapshots.push_back({component->id(), component->serialize()});
        } catch (const std::exception &error) {
            failure = error.what();
            return false;
        } catch (...) {
            failure = "unknown exception";
            return false;
        }
    }
    return true;
}

std::string restoreSnapshots(const std::vector<Snapshot> &snapshots,
                             std::span<IComponentEngine *const> components,
                             const NodeGraphEngine &graph) {
    std::string messages;
    for (const Snapshot &snapshot : snapshots) {
        IComponentEngine *target = nullptr;
        for (IComponentEngine *component : components) {
            if (component && component->id() == snapshot.id) {
                target = component;
                break;
            }
        }
        if (!target) {
            appendMessage(messages, componentLabel(snapshot.id) + "no longer present");
            continue;
        }
        try {
            target->deserialize(snapshot.state);
        } catch (const std::exception &error) {
            appendMessage(messages, componentLabel(snapshot.id) + error.what());
        } catch (...) {
            appendMessage(messages, componentLabel(snapshot.id) + "unknown exception");
        }
    }
    rewireComponentInputs(components, graph);
    return messages;
}

} // namespace

BoundaryOutcome RunFlowWithinBoundary(const FlowSpec &spec,
                                      std::span<IComponentEngine *const> components,
                                      const NodeGraphEngine &graph) {
    std::vector<Snapshot> snapshots;
    std::string snapshot_error;
    if (!captureSnapshots(components, snapshots, snapshot_error))
        return BoundaryOutcome{BoundaryStatus::SnapshotFailed, {}, std::move(snapshot_error)};

    // 2. Run the flow. An exception is recorded here, and restoration still runs.
    BoundaryOutcome outcome;
    bool execution_failed = false;
    try {
        outcome.result = RunFlow(spec, components, graph);
    } catch (const std::exception &error) {
        execution_failed = true;
        outcome.message = error.what();
    } catch (...) {
        execution_failed = true;
        outcome.message = "unknown exception";
    }

    const std::string restore_messages = restoreSnapshots(snapshots, components, graph);
    // 5. A restore error outranks the run's own outcome and discards its result.
    if (!restore_messages.empty())
        return BoundaryOutcome{BoundaryStatus::RestoreFailed, {}, std::move(restore_messages)};

    // 6. Otherwise the outcome is the run's own: completed, or an execution failure.
    if (execution_failed)
        return BoundaryOutcome{BoundaryStatus::ExecutionFailed, {}, std::move(outcome.message)};
    outcome.status = BoundaryStatus::Completed;
    return outcome;
}

struct FlowRunSession::Impl {
    Impl(const FlowSpec &flow, std::span<IComponentEngine *const> live_components,
         const NodeGraphEngine &live_graph)
        : spec(flow), components(live_components.begin(), live_components.end()),
          graph(&live_graph), counter(spec.conditions.size(), 0) {
        std::string snapshot_error;
        if (!captureSnapshots(componentView(), snapshots, snapshot_error)) {
            outcome = {BoundaryStatus::SnapshotFailed, {}, std::move(snapshot_error)};
            is_complete = true;
            return;
        }

        try {
            const std::vector<FlowError> issues = ValidateFlow(spec, componentView());
            if (!issues.empty()) {
                outcome.result.name = spec.name;
                outcome.result.error = issues.front();
                completeWithRestore();
                return;
            }
        } catch (const std::exception &error) {
            failWithRestore(BoundaryStatus::ExecutionFailed, error.what());
            return;
        } catch (...) {
            failWithRestore(BoundaryStatus::ExecutionFailed, "unknown exception");
            return;
        }

        for (const Condition &condition : spec.conditions) {
            const auto count = static_cast<std::uint64_t>(condition.values.size());
            if (count == 0) {
                row_count = 0;
                break;
            }
            if (row_count > std::numeric_limits<std::uint64_t>::max() / count) {
                row_count = std::numeric_limits<std::uint64_t>::max();
                break;
            }
            row_count *= count;
        }
        outcome.result.name = spec.name;
    }

    std::span<IComponentEngine *const> componentView() const {
        return {components.data(), components.size()};
    }

    void completeWithRestore() {
        const std::string restore_error = restoreSnapshots(snapshots, componentView(), *graph);
        if (!restore_error.empty()) {
            outcome.status = BoundaryStatus::RestoreFailed;
            outcome.message = restore_error;
            outcome.result = {};
        }
        is_complete = true;
    }

    void failWithRestore(BoundaryStatus status, std::string message) {
        const std::string restore_error = restoreSnapshots(snapshots, componentView(), *graph);
        outcome.result = {};
        if (!restore_error.empty()) {
            outcome.status = BoundaryStatus::RestoreFailed;
            outcome.message = restore_error;
        } else {
            outcome.status = status;
            outcome.message = std::move(message);
        }
        is_complete = true;
    }

    void advanceCounter() {
        for (std::size_t index = counter.size(); index > 0;) {
            --index;
            if (++counter[index] < spec.conditions[index].values.size())
                return;
            counter[index] = 0;
        }
    }

    FlowSpec rowSpec() const {
        FlowSpec row{spec.version, spec.name, {}, spec.measure};
        row.conditions.reserve(spec.conditions.size());
        for (std::size_t index = 0; index < spec.conditions.size(); ++index) {
            Condition condition = spec.conditions[index];
            condition.values = {condition.values[counter[index]]};
            row.conditions.push_back(std::move(condition));
        }
        return row;
    }

    bool advance(std::size_t max_rows, std::chrono::steady_clock::duration budget) {
        if (is_complete || max_rows == 0)
            return is_complete;

        const auto started = std::chrono::steady_clock::now();
        std::size_t processed = 0;
        std::optional<FlowError> flow_error;
        std::string execution_error;
        while (rows_completed < row_count && processed < max_rows) {
            FlowResult row_result;
            try {
                row_result = RunFlow(rowSpec(), componentView(), *graph);
            } catch (const std::exception &error) {
                execution_error = error.what();
                break;
            } catch (...) {
                execution_error = "unknown exception";
                break;
            }
            if (!row_result.ok) {
                flow_error = row_result.error;
                break;
            }
            if (row_result.rows.empty()) {
                execution_error = "flow produced no row";
                break;
            }
            outcome.result.rows.push_back(std::move(row_result.rows.front()));
            const std::string row_restore_error =
                restoreSnapshots(snapshots, componentView(), *graph);
            if (!row_restore_error.empty()) {
                outcome = {BoundaryStatus::RestoreFailed, {}, row_restore_error};
                is_complete = true;
                return true;
            }
            ++rows_completed;
            ++processed;
            if (rows_completed < row_count)
                advanceCounter();
            if (rows_completed < row_count && std::chrono::steady_clock::now() - started >= budget)
                break;
        }

        if (!execution_error.empty() || flow_error) {
            const std::string restore_error = restoreSnapshots(snapshots, componentView(), *graph);
            if (!restore_error.empty()) {
                outcome = {BoundaryStatus::RestoreFailed, {}, restore_error};
                is_complete = true;
            } else if (!execution_error.empty()) {
                outcome = {BoundaryStatus::ExecutionFailed, {}, std::move(execution_error)};
                is_complete = true;
            } else {
                outcome.result.rows.clear();
                outcome.result.ok = false;
                outcome.result.error = std::move(*flow_error);
                outcome.status = BoundaryStatus::Completed;
                is_complete = true;
            }
        } else if (rows_completed == row_count) {
            outcome.result.ok = true;
            outcome.status = BoundaryStatus::Completed;
            is_complete = true;
        }
        return is_complete;
    }

    void cancel() {
        if (is_complete)
            return;
        failWithRestore(BoundaryStatus::ExecutionFailed, "flow run cancelled");
    }

    void abandon() noexcept {
        snapshots.clear();
        components.clear();
        graph = nullptr;
        outcome = {BoundaryStatus::ExecutionFailed, {}, "flow run abandoned"};
        is_complete = true;
    }

    FlowSpec spec;
    std::vector<IComponentEngine *> components;
    const NodeGraphEngine *graph;
    std::vector<Snapshot> snapshots;
    BoundaryOutcome outcome;
    std::vector<std::size_t> counter;
    std::uint64_t row_count = 1;
    std::uint64_t rows_completed = 0;
    bool is_complete = false;
};

FlowRunSession::FlowRunSession(const FlowSpec &spec, std::span<IComponentEngine *const> components,
                               const NodeGraphEngine &graph)
    : m_impl(std::make_unique<Impl>(spec, components, graph)) {}

FlowRunSession::~FlowRunSession() = default;

bool FlowRunSession::advance(std::size_t max_rows, std::chrono::steady_clock::duration budget) {
    return m_impl->advance(max_rows, budget);
}

bool FlowRunSession::complete() const { return m_impl->is_complete; }

const BoundaryOutcome &FlowRunSession::outcome() const { return m_impl->outcome; }

void FlowRunSession::cancel() { m_impl->cancel(); }

void FlowRunSession::abandon() noexcept { m_impl->abandon(); }
