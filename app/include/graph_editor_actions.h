#pragma once

#include "circuit_runtime.h"
#include "editor_history.h"
#include <map>
#include <string>
#include <vector>

class GraphEditorActions {
  public:
    explicit GraphEditorActions(CircuitRuntime &runtime);

    bool addProbePin(int pin_id);
    bool removeProbePin(int pin_id);
    void clearProbes();
    // Clears the probes, then re-adds `pins` in order so slot order matches the vector.
    void setProbePins(const std::vector<int> &pins);

    int createGroup(std::string name, std::vector<int> member_node_ids);
    // Recreates a removed group under its original id and state. Returns group_id, or -1 when
    // that id is live or the engine refuses the members.
    int restoreGroup(int group_id, const GroupState &state);
    bool removeGroup(int group_id);
    bool renameGroup(int group_id, std::string name);
    bool setGroupCollapsed(int group_id, bool collapsed);
    void selectGroup(int group_id);
    bool setNodePartNumber(int node_id, std::string part_number);

    void resetForProjectReplacement();
    void topologyChanged();
    // Moves the probe and group references of a node that an in-place replacement retires onto
    // its successor (see NodeGraphEngine::remapNodeReferences()).
    void replaceNodeReferences(int old_node_id, int new_node_id, const std::map<int, int> &pin_map);

  private:
    CircuitRuntime &m_runtime;
};
