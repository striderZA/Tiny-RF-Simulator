#include "imnodes.h"
#include "node_graph_widget.h"
#include <cstddef>
#include <utility>

// Align and distribute for the widget's node selection (TRS-79). The geometry is in node_layout.h;
// this file reads the selection, refuses selections that hide nodes, and hands the moves to the
// app's command boundary.

namespace {

LayoutPoint toLayoutPoint(ImVec2 v) { return LayoutPoint{v.x, v.y}; }

LayoutSize toLayoutSize(ImVec2 v) { return LayoutSize{v.x, v.y}; }

} // namespace

void NodeGraphWidget::alignSelection(AlignMode mode) {
    runLayout([mode](const std::vector<LayoutItem> &items) { return alignNodes(items, mode); });
}

void NodeGraphWidget::distributeSelection(DistributeAxis axis) {
    runLayout(
        [axis](const std::vector<LayoutItem> &items) { return distributeNodes(items, axis); });
}

void NodeGraphWidget::runLayout(
    const std::function<std::vector<LayoutMove>(const std::vector<LayoutItem> &)> &layout) {
    ImNodes::EditorContextSet(m_context);
    const int count = ImNodes::NumSelectedNodes();
    std::vector<int> selected(static_cast<std::size_t>(count));
    if (count > 0)
        ImNodes::GetSelectedNodes(selected.data());

    // A collapsed block is an ImNodes node, so it can be in the selection; its members are hidden
    // and cannot be laid out either. Refuse the whole selection rather than moving part of it.
    std::vector<LayoutItem> items;
    for (int id : selected) {
        if (const Group *block = m_engine.groupById(id)) {
            showLayoutMessage("Cannot align or distribute collapsed subcircuit \"" + block->name +
                              "\". Expand it first.");
            return;
        }
        const int group_id = m_engine.groupIdForNode(id);
        if (group_id != -1 && m_engine.isGroupCollapsed(group_id)) {
            showLayoutMessage("Cannot align or distribute nodes inside collapsed subcircuit \"" +
                              m_engine.groupById(group_id)->name + "\". Expand it first.");
            return;
        }
        items.push_back(LayoutItem{id, toLayoutPoint(editorPositionForSave(id)),
                                   toLayoutSize(cachedNodeDimensions(id))});
    }
    if (items.size() < 2 || !m_actions.moveNodes)
        return;

    const std::string error = m_actions.moveNodes(layout(items));
    if (!error.empty())
        showLayoutMessage(error);
}

void NodeGraphWidget::showLayoutMessage(std::string message) {
    m_layout_message = std::move(message);
    m_open_layout_message = true;
}
