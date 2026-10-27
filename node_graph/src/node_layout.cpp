#include "node_layout.h"
#include <algorithm>
#include <cstddef>
#include <limits>
#include <numeric>

namespace {

// One coordinate of the layout: x for the horizontal axis, y for the vertical axis.
struct Axis {
    bool horizontal;

    float lead(const LayoutItem &item) const {
        return horizontal ? item.position.x : item.position.y;
    }
    float extent(const LayoutItem &item) const { return horizontal ? item.size.x : item.size.y; }
    void setLead(LayoutMove &move, float value) const {
        (horizontal ? move.position.x : move.position.y) = value;
    }
};

std::vector<LayoutMove> unchangedMoves(const std::vector<LayoutItem> &items) {
    std::vector<LayoutMove> moves;
    moves.reserve(items.size());
    for (const LayoutItem &item : items)
        moves.push_back({item.node_id, item.position});
    return moves;
}

} // namespace

std::vector<LayoutMove> alignNodes(const std::vector<LayoutItem> &items, AlignMode mode) {
    std::vector<LayoutMove> moves = unchangedMoves(items);
    if (items.size() < 2)
        return moves;

    const bool horizontal = mode == AlignMode::Left || mode == AlignMode::Right ||
                            mode == AlignMode::CentreHorizontally;
    const Axis axis{horizontal};

    float low = std::numeric_limits<float>::infinity();
    float high = -std::numeric_limits<float>::infinity();
    for (const LayoutItem &item : items) {
        low = std::min(low, axis.lead(item));
        high = std::max(high, axis.lead(item) + axis.extent(item));
    }

    for (std::size_t i = 0; i < items.size(); ++i) {
        const float extent = axis.extent(items[i]);
        float target = low;
        switch (mode) {
        case AlignMode::Left:
        case AlignMode::Top:
            target = low;
            break;
        case AlignMode::Right:
        case AlignMode::Bottom:
            target = high - extent;
            break;
        case AlignMode::CentreHorizontally:
        case AlignMode::CentreVertically:
            target = (low + high) * 0.5f - extent * 0.5f;
            break;
        }
        axis.setLead(moves[i], target);
    }
    return moves;
}

std::vector<LayoutMove> distributeNodes(const std::vector<LayoutItem> &items,
                                        DistributeAxis axis_id) {
    std::vector<LayoutMove> moves = unchangedMoves(items);
    if (items.size() < 3)
        return moves;

    const Axis axis{axis_id == DistributeAxis::Horizontal};

    // Visit items in order of leading edge; equal edges fall back to node id so the result does
    // not depend on the caller's order.
    std::vector<std::size_t> order(items.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const float la = axis.lead(items[a]);
        const float lb = axis.lead(items[b]);
        if (la != lb)
            return la < lb;
        return items[a].node_id < items[b].node_id;
    });

    const LayoutItem &first = items[order.front()];
    const LayoutItem &last = items[order.back()];
    float interior_extent = 0.0f;
    for (std::size_t k = 1; k + 1 < order.size(); ++k)
        interior_extent += axis.extent(items[order[k]]);

    // Free space between the first node's trailing edge and the last node's leading edge, shared
    // equally by the (n - 1) gaps.
    const float free_space =
        axis.lead(last) - (axis.lead(first) + axis.extent(first)) - interior_extent;
    const float gap = free_space / static_cast<float>(items.size() - 1);

    float cursor = axis.lead(first) + axis.extent(first) + gap;
    for (std::size_t k = 1; k + 1 < order.size(); ++k) {
        const std::size_t i = order[k];
        axis.setLead(moves[i], cursor);
        cursor += axis.extent(items[i]) + gap;
    }
    return moves;
}
