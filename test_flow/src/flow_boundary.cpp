#include "flow_boundary.h"

#include "flow_runner.h"
#include "rewire.h"

#include <exception>
#include <nlohmann/json.hpp>
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

} // namespace

BoundaryOutcome RunFlowWithinBoundary(const FlowSpec &spec,
                                      std::span<IComponentEngine *const> components,
                                      const NodeGraphEngine &graph) {
    // 1. Snapshot every live engine before anything runs. A failure refuses the run.
    std::vector<Snapshot> snapshots;
    snapshots.reserve(components.size());
    for (IComponentEngine *component : components) {
        if (!component)
            continue;
        try {
            snapshots.push_back({component->id(), component->serialize()});
        } catch (const std::exception &error) {
            return BoundaryOutcome{BoundaryStatus::SnapshotFailed, {}, error.what()};
        } catch (...) {
            return BoundaryOutcome{BoundaryStatus::SnapshotFailed, {}, "unknown exception"};
        }
    }

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

    // 3. Restore each snapshot independently, looked up by id. A failure never stops later
    //    restores, so one bad engine cannot strand the rest of the circuit at swept values.
    std::string restore_messages;
    for (const Snapshot &snapshot : snapshots) {
        IComponentEngine *target = nullptr;
        for (IComponentEngine *component : components) {
            if (component && component->id() == snapshot.id) {
                target = component;
                break;
            }
        }
        if (!target) {
            appendMessage(restore_messages, componentLabel(snapshot.id) + "no longer present");
            continue;
        }
        try {
            target->deserialize(snapshot.state);
        } catch (const std::exception &error) {
            appendMessage(restore_messages, componentLabel(snapshot.id) + error.what());
        } catch (...) {
            appendMessage(restore_messages, componentLabel(snapshot.id) + "unknown exception");
        }
    }

    // 4. Always rewire, whether or not the flow or a restore failed.
    rewireComponentInputs(components, graph);

    // 5. A restore error outranks the run's own outcome and discards its result.
    if (!restore_messages.empty())
        return BoundaryOutcome{BoundaryStatus::RestoreFailed, {}, std::move(restore_messages)};

    // 6. Otherwise the outcome is the run's own: completed, or an execution failure.
    if (execution_failed)
        return BoundaryOutcome{BoundaryStatus::ExecutionFailed, {}, std::move(outcome.message)};
    outcome.status = BoundaryStatus::Completed;
    return outcome;
}
