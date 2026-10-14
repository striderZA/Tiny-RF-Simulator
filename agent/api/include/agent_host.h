#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

struct AgentCall {
    std::string tool;
    nlohmann::ordered_json arguments;
    std::string client;
};

struct AgentPlacement {
    int graph_node_id = -1;
    std::optional<std::array<float, 2>> position;
    int column = 0;
    int row = 0;
};

struct AgentActivity {
    std::chrono::system_clock::time_point time;
    std::string tool, client, summary;
    bool ok = true;
    std::string error_code;
    double duration_ms = 0.0;
};

enum class AgentReplacementCause { NewProject, OpenedProject, Tutorial, Reverted };

// Outcome of an agent-requested project load. `cleared` means the open project was reset before
// the failure, so the circuit was replaced even though the load did not succeed.
struct AgentProjectLoadResult {
    bool loaded = false;
    bool cleared = false;
};

class IAgentHost {
  public:
    virtual ~IAgentHost() = default;

    virtual void beginCheckpoint(const AgentCall &) = 0;
    virtual void commitCheckpoint(const std::string &summary) = 0;
    virtual void discardCheckpoint() = 0;
    virtual void placeComponents(const std::vector<AgentPlacement> &) = 0;
    virtual std::array<float, 2> nodeEditorSpacePosition(int graph_node_id) const = 0;
    // Returns whether the component's stored editor position changed.
    virtual bool setNodeEditorSpacePosition(int graph_node_id, std::array<float, 2> position) = 0;
    virtual bool appModalOpen() const = 0;
    virtual std::optional<std::string> projectName() const = 0;
    // Project files use the GUI's save and load paths without dialogs. saveProjectFile returns
    // false when the file cannot be written; the previous path and dirty state are kept.
    virtual std::optional<std::string> projectPath() const = 0;
    virtual bool saveProjectFile(const std::string &path) = 0;
    virtual AgentProjectLoadResult loadProjectFile(const std::string &path) = 0;
    virtual void recordActivity(const AgentActivity &) = 0;
};
