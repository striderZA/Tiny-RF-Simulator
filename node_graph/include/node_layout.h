#pragma once

#include <vector>

// Pure geometry for the node editor's align and distribute commands. Positions are editor-space
// top-left corners and sizes are node width and height, both in ImGui logical pixels. This header
// has no ImGui types, so the layout maths is testable without a context.

struct LayoutPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct LayoutSize {
    float x = 0.0f;
    float y = 0.0f;
};

struct LayoutItem {
    int node_id = -1;
    LayoutPoint position;
    LayoutSize size;
};

// A node placed at `position`. The layout functions return one move per input item, including
// items that stay where they are, so a caller can check the whole selection.
struct LayoutMove {
    int node_id = -1;
    LayoutPoint position;
};

enum class AlignMode {
    Left,
    Right,
    Top,
    Bottom,
    CentreHorizontally, // centres on one vertical line: shared x
    CentreVertically,   // centres on one horizontal line: shared y
};

enum class DistributeAxis { Horizontal, Vertical };

// Aligns every item to the selection's bounding box: the leftmost or rightmost edge, or the
// centre of that span. Fewer than two items come back unchanged.
std::vector<LayoutMove> alignNodes(const std::vector<LayoutItem> &items, AlignMode mode);

// Keeps the two outermost items fixed (lowest and highest leading edge) and spaces the others so
// the gaps between neighbouring nodes are equal. Fewer than three items come back unchanged.
std::vector<LayoutMove> distributeNodes(const std::vector<LayoutItem> &items, DistributeAxis axis);
