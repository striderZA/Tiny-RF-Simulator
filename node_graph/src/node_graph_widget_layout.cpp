#include "imnodes.h"
#include "node_graph_widget.h"
#include <cstddef>
#include <utility>

// Align, distribute and snap-to-grid for the widget's node selection (TRS-79, TRS-80). The align
// geometry is in node_layout.h. This file reads the selection, refuses selections that hide nodes,
// and hands align moves to the app's command boundary. Snap rounds with ImNodes and reports the
// change through onNodeMoved, as a drag does.
//
// ImNodes keeps a selection entry after its node leaves the pool: a node hidden by a collapse or
// removed from the graph reads back as an invalid id (INT_MIN), and its slot is reused later. Such
// entries are not nodes, so they are dropped here. The subcircuit a hidden member belonged to is
// remembered at the start of each draw, while the member still reads as selected.

namespace {

LayoutPoint toLayoutPoint(ImVec2 v) { return LayoutPoint{v.x, v.y}; }

LayoutSize toLayoutSize(ImVec2 v) { return LayoutSize{v.x, v.y}; }

std::string collapsedMemberMessage(const std::string &group_name) {
    return "Cannot align or distribute nodes inside collapsed subcircuit \"" + group_name +
           "\". Expand it first.";
}

std::string collapsedBlockMessage(const std::string &group_name) {
    return "Cannot align or distribute collapsed subcircuit \"" + group_name +
           "\". Expand it first.";
}

} // namespace

void NodeGraphWidget::alignSelection(AlignMode mode) {
    runLayout([mode](const std::vector<LayoutItem> &items) { return alignNodes(items, mode); });
}

void NodeGraphWidget::distributeSelection(DistributeAxis axis) {
    runLayout(
        [axis](const std::vector<LayoutItem> &items) { return distributeNodes(items, axis); });
}

void NodeGraphWidget::snapSelectionToGrid() {
    bool has_stale = false;
    const std::vector<int> live = liveSelection(&has_stale);
    const std::string refusal = layoutRefusal(live, has_stale);
    if (!refusal.empty()) {
        showLayoutMessage(refusal);
        return;
    }

    // ImNodes rounds an origin only while GridSnapping is set, and drag snapping may be off. Set
    // the bit for these calls and restore it afterwards.
    ImNodesStyle &style = ImNodes::GetStyle();
    const ImNodesStyleFlags saved_flags = style.Flags;
    style.Flags |= ImNodesStyleFlags_GridSnapping;
    bool moved = false;
    for (int id : live) {
        // SnapNodeToGrid would create a pool entry at the origin for an unknown node, so only
        // nodes already placed in the pool are rounded.
        if (!m_registered_in_pool.count(id))
            continue;
        const ImVec2 before = ImNodes::GetNodeGridSpacePos(id);
        ImNodes::SnapNodeToGrid(id);
        const ImVec2 after = ImNodes::GetNodeGridSpacePos(id);
        moved = moved || after.x != before.x || after.y != before.y;
    }
    style.Flags = saved_flags;

    if (moved && onNodeMoved)
        onNodeMoved();
}

int NodeGraphWidget::layoutSelectionCount() const {
    bool has_stale = false;
    return static_cast<int>(liveSelection(&has_stale).size());
}

bool NodeGraphWidget::isLiveSelectionEntry(int id) const {
    // A collapsed block is an ImNodes node with the group's id. An expanded group draws no block,
    // so an entry for one is left over from the collapsed state.
    if (const Group *block = m_engine.groupById(id))
        return block->collapsed;
    for (const auto &node : m_engine.nodes()) {
        if (node.node_id == id)
            return true;
    }
    return false;
}

std::vector<int> NodeGraphWidget::liveSelection(bool *has_stale) const {
    ImNodes::EditorContextSet(m_context);
    const int count = ImNodes::NumSelectedNodes();
    std::vector<int> raw(static_cast<std::size_t>(count));
    if (count > 0)
        ImNodes::GetSelectedNodes(raw.data());

    std::vector<int> live;
    bool stale = false;
    for (int id : raw) {
        if (isLiveSelectionEntry(id))
            live.push_back(id);
        else
            stale = true;
    }
    if (has_stale)
        *has_stale = stale;
    return live;
}

void NodeGraphWidget::rememberCollapsedSelection() {
    bool has_stale = false;
    const std::vector<int> live = liveSelection(&has_stale);
    // With no stale entries the selection is current, so the memory is replaced. Once entries have
    // gone stale, the members they stood for are no longer in the selection, so the memory only
    // grows until the selection is cleared.
    if (!has_stale)
        m_collapsed_selection_groups.clear();
    for (int id : live) {
        if (m_engine.groupById(id)) {
            m_collapsed_selection_groups.insert(id);
            continue;
        }
        for (int group_id : m_engine.groupsContainingNode(id)) {
            if (m_engine.isGroupCollapsed(group_id))
                m_collapsed_selection_groups.insert(group_id);
        }
    }
}

std::string NodeGraphWidget::layoutRefusal(const std::vector<int> &live, bool has_stale) const {
    for (int id : live) {
        if (const Group *block = m_engine.groupById(id))
            return collapsedBlockMessage(block->name);
    }
    for (int id : live) {
        for (int group_id : m_engine.groupsContainingNode(id)) {
            if (m_engine.isGroupCollapsed(group_id))
                return collapsedMemberMessage(m_engine.groupById(group_id)->name);
        }
    }
    if (has_stale) {
        // The selection lost hidden members to a collapse. Only a subcircuit that is still
        // collapsed refuses; one expanded since is no longer hiding anything.
        for (int group_id : m_collapsed_selection_groups) {
            if (m_engine.isGroupCollapsed(group_id))
                return collapsedMemberMessage(m_engine.groupById(group_id)->name);
        }
    }
    return {};
}

void NodeGraphWidget::runLayout(
    const std::function<std::vector<LayoutMove>(const std::vector<LayoutItem> &)> &layout) {
    bool has_stale = false;
    const std::vector<int> live = liveSelection(&has_stale);

    const std::string refusal = layoutRefusal(live, has_stale);
    if (!refusal.empty()) {
        showLayoutMessage(refusal);
        return;
    }
    if (live.size() < 2 || !m_actions.moveNodes)
        return;

    std::vector<LayoutItem> items;
    items.reserve(live.size());
    for (int id : live) {
        items.push_back(LayoutItem{id, toLayoutPoint(editorPositionForSave(id)),
                                   toLayoutSize(cachedNodeDimensions(id))});
    }
    const std::string error = m_actions.moveNodes(layout(items));
    if (!error.empty())
        showLayoutMessage(error);
}

void NodeGraphWidget::showLayoutMessage(std::string message) {
    m_layout_message = std::move(message);
    m_open_layout_message = true;
}
