#pragma once

#include <string>
#include <vector>

struct GroupBoundaryPin {
    int id;               // synthesized, unique within the group; >= 100000
    int internal_node_id; // the in-group node that owns the internal pin
    int internal_pin_id;  // the engine's real pin id
    bool is_output;       // true = subcircuit output (link source is in-group)
    std::string label;    // "<internal node label> <pin label>"
};

struct Group {
    int id;                           // allocated by the engine from m_next_group_id; >= 50000
    std::string name;                 // user-editable; default "Subcircuit N"
    std::vector<int> member_node_ids; // frozen after creation (snapshot model)
    std::vector<GroupBoundaryPin> boundary_pins; // recomputed by rebuildGroupBoundaryPins
    bool collapsed = false;
};
