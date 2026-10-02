#pragma once

#include "circuit_runtime.h"
#include <string>
#include <vector>

class GraphEditorActions {
  public:
    explicit GraphEditorActions(CircuitRuntime &runtime);

    bool addProbePin(int pin_id);
    bool removeProbePin(int pin_id);
    void clearProbes();

    int createGroup(std::string name, std::vector<int> member_node_ids);
    bool removeGroup(int group_id);
    bool renameGroup(int group_id, std::string name);
    bool setGroupCollapsed(int group_id, bool collapsed);
    void selectGroup(int group_id);
    bool setNodePartNumber(int node_id, std::string part_number);

    void resetForProjectReplacement();
    void topologyChanged();

  private:
    CircuitRuntime &m_runtime;
};
