#pragma once

#include <string>
#include <vector>

// Captured state of one subcircuit group, used to recreate it under its
// original id after an undo removes it.
struct GroupState {
    std::string name;
    std::vector<int> members;
    bool collapsed = false;
    bool operator==(const GroupState &) const = default;
};
