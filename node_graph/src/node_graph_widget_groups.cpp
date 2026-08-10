#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imnodes.h"
#include "logging_core.h"
#include "node_graph_widget.h"
#include <algorithm>
#include <limits>

void NodeGraphWidget::rebuildSynthMaps() {
    m_synth_pin_to_real_pin.clear();
    m_real_to_synth_pin.clear();
    for (const auto &g : m_engine.groups()) {
        // Only COLLAPSED groups have registered synthetic pins; expanded groups'
        // boundary pins are cleared (by setGroupCollapsed), but guard anyway.
        if (!g.collapsed)
            continue;
        for (const auto &bp : g.boundary_pins) {
            m_synth_pin_to_real_pin[bp.id] = bp.internal_pin_id;
            // Only one synthetic pin per internal pin (the first wins; if the same
            // internal pin appears in multiple boundary pins due to multiple outgoing
            // links, the link rendering can still find the synth pin)
            if (m_real_to_synth_pin.find(bp.internal_pin_id) == m_real_to_synth_pin.end()) {
                m_real_to_synth_pin[bp.internal_pin_id] = bp.id;
            }
        }
    }
}

void NodeGraphWidget::drawGroupBackgrounds() {
    // Group background is drawn here; internals (when expanded) are drawn by
    // drawNodes() afterward, on top of this background.
    ImDrawList *dl = ImGui::GetWindowDrawList();
    for (const auto &g : m_engine.groups()) {
        if (g.collapsed)
            continue;
        if (g.member_node_ids.empty())
            continue;

        // Compute bounding box in screen space from cached screen-space positions
        ImVec2 top_left(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        ImVec2 bottom_right(std::numeric_limits<float>::lowest(),
                            std::numeric_limits<float>::lowest());
        const float NODE_W = 120.0f, NODE_H = 80.0f; // approximate; refined in spike
        for (int nid : g.member_node_ids) {
            auto pos_it = m_node_screen_positions.find(nid);
            if (pos_it == m_node_screen_positions.end())
                continue;
            ImVec2 pos = pos_it->second;
            top_left.x = std::min(top_left.x, pos.x);
            top_left.y = std::min(top_left.y, pos.y);
            bottom_right.x = std::max(bottom_right.x, pos.x + NODE_W);
            bottom_right.y = std::max(bottom_right.y, pos.y + NODE_H);
        }
        if (top_left.x > bottom_right.x)
            continue; // no valid members

        ImVec2 pad(16, 16);
        ImVec2 tl_screen = top_left - pad;
        ImVec2 br_screen = bottom_right + pad;

        dl->AddRectFilled(tl_screen, br_screen, IM_COL32(80, 80, 120, 24));
        dl->AddRect(tl_screen, br_screen, IM_COL32(120, 120, 180, 96));

        // Title bar at the top of the rectangle (in screen space)
        drawGroupTitleBar(const_cast<Group &>(g), top_left);
    }
}

void NodeGraphWidget::drawGroupCollapsedBlocks() {
    for (const auto &g : m_engine.groups()) {
        if (!g.collapsed)
            continue;

        // Compute centroid in screen space using cached positions (member nodes may have been
        // removed from the imnodes pool since hidden nodes aren't rendered each frame)
        if (g.member_node_ids.empty())
            continue;
        ImVec2 sum(0, 0);
        int count = 0;
        for (int nid : g.member_node_ids) {
            auto pos_it = m_node_screen_positions.find(nid);
            if (pos_it == m_node_screen_positions.end())
                continue;
            sum.x += pos_it->second.x;
            sum.y += pos_it->second.y;
            ++count;
        }
        if (count == 0)
            continue;
        ImVec2 centroid_screen(sum.x / count, sum.y / count);
        // Convert to grid space and position the collapsed block at the centroid
        ImVec2 centroid_grid = centroid_screen - m_grid_to_screen_offset;
        ImNodes::SetNodeGridSpacePos(g.id, centroid_grid - ImVec2(60, 40));

        // Render the block as an imnodes node
        ImNodes::BeginNode(g.id);
        const ImU32 group_color = static_cast<ImU32>(themeColor(NodeKind::GroupCollapsed));
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, group_color);
        ImNodes::PushColorStyle(ImNodesCol_NodeOutline, group_color);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(g.name.c_str());
        ImNodes::EndNodeTitleBar();

        // Render boundary pins inside the body. In imnodes, the pin circle is drawn at the
        // left/right edge of the node and the ImGui text content you put between
        // Begin{Input,Output}Attribute/End{Input,Output}Attribute becomes the pin's label.
        // Layout: inputs on the left (vertically stacked), outputs on the right.
        if (g.boundary_pins.empty()) {
            // Fallback indicator so the collapsed block doesn't look broken when the
            // group has no cross-boundary links
            ImGui::Dummy(ImVec2(120, 30));
            ImGui::Indent(8);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 180, 200, 180));
            ImGui::TextUnformatted("(no external connections)");
            ImGui::PopStyleColor();
            ImGui::Unindent(8);
        } else {
            for (const auto &bp : g.boundary_pins) {
                int slot = m_engine.probeSlotForPin(bp.internal_pin_id);
                if (slot >= 0) {
                    static const ImU32 probe_colors[4] = {
                        IM_COL32(22, 199, 154, 255),
                        IM_COL32(230, 150, 40, 255),
                        IM_COL32(120, 50, 170, 255),
                        IM_COL32(60, 140, 220, 255),
                    };
                    ImNodes::PushColorStyle(ImNodesCol_Pin, probe_colors[slot]);
                    ImNodes::PushColorStyle(ImNodesCol_PinHovered, probe_colors[slot]);
                }
                if (bp.is_output) {
                    ImNodes::BeginOutputAttribute(bp.id);
                    ImGui::TextUnformatted(bp.label.c_str());
                    ImNodes::EndOutputAttribute();
                } else {
                    ImNodes::BeginInputAttribute(bp.id);
                    ImGui::TextUnformatted(bp.label.c_str());
                    ImNodes::EndInputAttribute();
                }
                if (slot >= 0) {
                    ImNodes::PopColorStyle();
                    ImNodes::PopColorStyle();
                }
            }
        }

        // Schematic symbol in the body (same machinery as drawNodes).
        // ponytail: hardcoded body rect — see plain-node draw for upgrade path.
        ImVec2 block_screen_pos = ImNodes::GetNodeScreenSpacePos(g.id);
        constexpr float BODY_W = 120.0f;
        constexpr float BODY_H = 60.0f;
        constexpr float TITLE_BAR_H = 24.0f;
        ImVec2 body_center(block_screen_pos.x + BODY_W * 0.5f,
                           block_screen_pos.y + TITLE_BAR_H + BODY_H * 0.5f);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        drawSchematicSymbol(dl, body_center, NodeKind::GroupCollapsed, group_color);

        // Expand button in the body. Placing it in the title bar would conflict with
        // imnodes' title-bar drag handling. Right-click context menu still works as a fallback.
        ImGui::Dummy(ImVec2(0, 4)); // small vertical spacer
        if (ImGui::Button("Expand", ImVec2(120, 0))) {
            m_engine.setGroupCollapsed(g.id, false);
        }

        ImNodes::PopColorStyle(); // NodeOutline
        ImNodes::PopColorStyle(); // TitleBar
        ImNodes::EndNode();
    }
}

void NodeGraphWidget::drawGroupTitleBar(Group &g, const ImVec2 &top_left_screen) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 tl_screen = top_left_screen - ImVec2(16, 16);
    ImVec2 br_screen = top_left_screen + ImVec2(180, 8);

    // Background of the title bar
    dl->AddRectFilled(tl_screen, br_screen, IM_COL32(60, 60, 100, 200));

    // Group name
    dl->AddText(tl_screen + ImVec2(8, 4), IM_COL32(255, 255, 255, 255), g.name.c_str());

    // Collapse button (▼) — full-height clickable area on the right side of the title bar
    ImVec2 btn_min = ImVec2(br_screen.x - 24, tl_screen.y);
    ImVec2 btn_max = br_screen;
    ImVec2 mouse = ImGui::GetMousePos();
    bool btn_hovered = mouse.x >= btn_min.x && mouse.x <= btn_max.x && mouse.y >= btn_min.y &&
                       mouse.y <= btn_max.y;
    ImU32 btn_color = btn_hovered ? IM_COL32(130, 130, 180, 255) : IM_COL32(100, 100, 140, 255);
    dl->AddRectFilled(btn_min, btn_max, btn_color);
    // Center the glyph in the 24x24 button
    ImVec2 text_pos(btn_min.x + 8, btn_min.y + 4);
    dl->AddText(text_pos, IM_COL32(255, 255, 255, 255), "\xE2\x96\xBC"); // ▼

    // Hit-test the button
    if (ImGui::IsMouseClicked(0) && btn_hovered) {
        m_engine.setGroupCollapsed(g.id, true);
    }
}

void NodeGraphWidget::detectNodeMoves() {
    // Require BOTH a mouse release AND an actual position change.
    // This prevents false positives: clicking menu items won't trigger,
    // and stale position caches from a previous project load won't trigger
    // because there was no mouse release during the load process.
    if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        return;
    bool moved = false;
    for (const auto &node : m_engine.nodes()) {
        // Skip nodes not drawn this frame (newly added nodes haven't been
        // through BeginNode yet, so GetNodeEditorSpacePos would assert).
        if (m_node_screen_positions.find(node.node_id) == m_node_screen_positions.end())
            continue;
        ImVec2 current = ImNodes::GetNodeEditorSpacePos(node.node_id);
        auto it = m_last_node_grid_positions.find(node.node_id);
        if (it != m_last_node_grid_positions.end()) {
            float dx = current.x - it->second.x;
            float dy = current.y - it->second.y;
            if (dx * dx + dy * dy > 1.0f) {
                LOG_INFO("Node %d moved from (%.0f,%.0f) to (%.0f,%.0f)", node.node_id,
                         it->second.x, it->second.y, current.x, current.y);
                moved = true;
            }
        }
        m_last_node_grid_positions[node.node_id] = current;
    }
    if (moved && onNodeMoved)
        onNodeMoved();
}

void NodeGraphWidget::handleRubberBand(bool editor_hovered) {
    bool shift = ImGui::IsKeyDown(ImGuiKey_LeftShift);
    bool left_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool left_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

    // Start rubber-band on Shift + left-click down on empty space
    if (left_down && shift && editor_hovered && !m_rubber_band_active) {
        m_rubber_band_active = true;
        m_rubber_band_start = ImGui::GetMousePos();
        m_rubber_band_end = m_rubber_band_start;
    }

    // Update end position while dragging, draw the selection rectangle
    if (m_rubber_band_active && left_down) {
        m_rubber_band_end = ImGui::GetMousePos();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 a(std::min(m_rubber_band_start.x, m_rubber_band_end.x),
                 std::min(m_rubber_band_start.y, m_rubber_band_end.y));
        ImVec2 b(std::max(m_rubber_band_start.x, m_rubber_band_end.x),
                 std::max(m_rubber_band_start.y, m_rubber_band_end.y));
        dl->AddRectFilled(a, b, IM_COL32(100, 200, 255, 32));
        dl->AddRect(a, b, IM_COL32(100, 200, 255, 200));
    }

    // On release, collect enclosed nodes
    if (m_rubber_band_active && left_released) {
        m_rubber_band_active = false;
        m_rubber_band_members.clear();

        ImVec2 screen_a(std::min(m_rubber_band_start.x, m_rubber_band_end.x),
                        std::min(m_rubber_band_start.y, m_rubber_band_end.y));
        ImVec2 screen_b(std::max(m_rubber_band_start.x, m_rubber_band_end.x),
                        std::max(m_rubber_band_start.y, m_rubber_band_end.y));

        for (const auto &node : m_engine.nodes()) {
            if (m_engine.groupIdForNode(node.node_id) != -1)
                continue; // skip grouped
            auto pos_it = m_node_screen_positions.find(node.node_id);
            if (pos_it == m_node_screen_positions.end())
                continue;
            ImVec2 pos = pos_it->second;
            ImVec2 center = pos + ImVec2(60, 40); // approximate node center
            if (center.x >= screen_a.x && center.x <= screen_b.x && center.y >= screen_a.y &&
                center.y <= screen_b.y) {
                m_rubber_band_members.push_back(node.node_id);
            }
        }

        if (m_rubber_band_members.size() >= 2) {
            m_show_create_popup = true;
            ImGui::OpenPopup("CreateSubcircuit");
        }
    }
}

void NodeGraphWidget::handleGroupSelection() {
    int hovered_node = -1;
    if (ImGui::IsMouseClicked(0) && ImNodes::IsNodeHovered(&hovered_node)) {
        if (hovered_node >= 50000 && hovered_node < 100000) {
            // It's a group
            m_engine.setSelectedGroupId(hovered_node);
            ImNodes::ClearNodeSelection();
        } else {
            // It's a regular node; deselect any group
            m_engine.setSelectedGroupId(-1);
        }
    }
    if (ImGui::IsMouseClicked(0)) {
        bool editor_hovered = ImNodes::IsEditorHovered();
        int hovered_node = -1;
        bool node_hovered = ImNodes::IsNodeHovered(&hovered_node);
        int link_id = -1;
        bool link_hovered = ImNodes::IsLinkHovered(&link_id);
        if (editor_hovered && !node_hovered && !link_hovered) {
            m_engine.setSelectedGroupId(-1);
        }
    }
}
