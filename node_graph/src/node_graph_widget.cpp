#define IMGUI_DEFINE_MATH_OPERATORS
#include "node_graph_widget.h"
#include "imgui.h"
#include "imnodes.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>

NodeGraphWidget::NodeGraphWidget(NodeGraphEngine &engine) : m_engine(engine), m_context(nullptr) {
    m_context = ImNodes::EditorContextCreate();
    ImNodes::EditorContextSet(m_context);
}

NodeGraphWidget::~NodeGraphWidget() { ImNodes::EditorContextFree(m_context); }

void NodeGraphWidget::syncNodesFromEngine() {
    ImNodes::EditorContextSet(m_context);
    for (const auto &node : m_engine.nodes()) {
        // Register node with the imnodes pool if not already present.
        // SetNodeGridSpacePos uses ObjectPoolFindOrCreateObject internally,
        // so it creates the node if it doesn't exist. To avoid resetting
        // positions of already-registered nodes, we only call it the first
        // time we see a given node ID.
        if (m_registered_in_pool.insert(node.node_id).second) {
            ImNodes::SetNodeGridSpacePos(node.node_id, ImVec2(0, 0));
        }
    }
}

void NodeGraphWidget::markNodesRegistered() {
    for (const auto &node : m_engine.nodes()) {
        m_registered_in_pool.insert(node.node_id);
    }
}

void NodeGraphWidget::draw(const char *title, bool *p_open) {
    ImNodes::EditorContextSet(m_context);
    markNodesRegistered();

    if (ImGui::Begin(title, p_open)) {
        setupDarkTheme();
        rebuildSynthMaps();

        drawGroupBackgrounds();

        ImNodes::BeginNodeEditor();

        drawNodes();
        drawGroupCollapsedBlocks();
        drawLinks();

        // Cache editor hover state before EndNodeEditor (IsEditorHovered only works inside scope)
        bool editor_hovered = ImNodes::IsEditorHovered();

        ImNodes::EndNodeEditor();

        // Rubber-band selection (Shift+drag on empty space)
        handleRubberBand(editor_hovered);

        // Pin tooltips (after EndNodeEditor per imnodes query pattern)
        showPinTooltips();
        showNodeHoverTooltips();

        // Process interactions after EndNodeEditor (IsNodeHovered requires scope None)
        handleLinkCreation();
        handleLinkDeletion();
        handleProbeClick();
        handleGroupSelection();
        handleContextMenu(editor_hovered);
        handleNodeDeletion();
        detectNodeMoves();

        // "Create Subcircuit" popup after rubber-band selection
        if (m_show_create_popup) {
            if (ImGui::BeginPopupModal("CreateSubcircuit", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                static char name_buf[128];
                static int last_member_count = -1;
                if (last_member_count != static_cast<int>(m_rubber_band_members.size())) {
                    std::snprintf(name_buf, sizeof(name_buf), "Subcircuit %d",
                                  m_engine.numGroups() + 1);
                    last_member_count = static_cast<int>(m_rubber_band_members.size());
                }

                ImGui::Text("Members (%zu):", m_rubber_band_members.size());
                ImGui::Indent();
                for (int nid : m_rubber_band_members) {
                    for (const auto &n : m_engine.nodes()) {
                        if (n.node_id == nid) {
                            ImGui::TextUnformatted(n.label.c_str());
                            break;
                        }
                    }
                }
                ImGui::Unindent();

                ImGui::InputText("Name", name_buf, sizeof(name_buf));

                if (ImGui::Button("Create")) {
                    m_engine.addGroup(name_buf, m_rubber_band_members);
                    m_show_create_popup = false;
                    last_member_count = -1;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    m_show_create_popup = false;
                    last_member_count = -1;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
        // pop the 9 imnodes + 1 ImGui color styles pushed by setupDarkTheme() at frame start.
        // ponytail: explicit pop rather than relying on imnodes' frame-level reset,
        // which does NOT auto-pop user-pushed color styles.
        ImNodes::PopColorStyle(); // PinHovered
        ImNodes::PopColorStyle(); // Pin
        ImNodes::PopColorStyle(); // TitleBar
        ImNodes::PopColorStyle(); // NodeOutline
        ImNodes::PopColorStyle(); // NodeBackground
        ImNodes::PopColorStyle(); // Link
        ImNodes::PopColorStyle(); // GridLinePrimary
        ImNodes::PopColorStyle(); // GridLine
        ImNodes::PopColorStyle(); // GridBackground
        ImGui::PopStyleColor();   // WindowBg
    }
    ImGui::End();
}

NodeKind NodeGraphWidget::kindForLabel(const std::string &label) const {
    for (const auto &[prefix, kind] : m_kind_prefixes)
        if (label.rfind(prefix, 0) == 0)
            return kind;
    return NodeKind::Unknown;
}

void NodeGraphWidget::drawNodes() {
    // Clear screen position cache - will be repopulated for nodes drawn this frame.
    // This ensures detectNodeMoves() only checks nodes that were actually drawn,
    // not stale entries from previous frames.
    m_node_screen_positions.clear();

    std::unordered_set<int> hidden_nodes;
    for (const auto &g : m_engine.groups()) {
        if (g.collapsed) {
            for (int nid : g.member_node_ids)
                hidden_nodes.insert(nid);
        }
    }

    bool first_visible = true;
    for (const auto &node : m_engine.nodes()) {
        if (hidden_nodes.count(node.node_id))
            continue;

        ImNodes::BeginNode(node.node_id);

        // Cache screen-space positions after BeginNode registers the node in the imnodes
        // pool, so GetNodeScreenSpacePos/GetNodeGridSpacePos don't assert on first frame.
        // drawGroupBackgrounds and drawGroupCollapsedBlocks use this cache because hidden
        // nodes get removed from the pool by ObjectPoolUpdate in EndNodeEditor.
        ImVec2 screen_pos = ImNodes::GetNodeScreenSpacePos(node.node_id);
        m_node_screen_positions[node.node_id] = screen_pos;
        if (first_visible) {
            // Refresh the grid-to-screen offset from the first visible node every frame
            // so it's always current with panning changes.
            ImVec2 grid_pos = ImNodes::GetNodeGridSpacePos(node.node_id);
            m_grid_to_screen_offset = screen_pos - grid_pos;
            first_visible = false;
        }
        const NodeKind kind = kindForLabel(node.label);
        const ImU32 color = static_cast<ImU32>(themeColor(kind));
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, color);
        ImNodes::PushColorStyle(ImNodesCol_NodeOutline, color);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(node.label.c_str());
        ImNodes::EndNodeTitleBar();

        // Schematic symbol body. Centered in the body region.
        // ponytail: hardcoded body rect; upgrade when GetNodeBodyRect becomes available.
        constexpr float BODY_W = 96.0f;
        constexpr float BODY_H = 64.0f;
        constexpr float TITLE_BAR_H = 24.0f;
        float subtitle_offset = 0.0f;
        if (!node.part_number.empty())
            subtitle_offset = ImGui::GetTextLineHeightWithSpacing();
        ImVec2 body_center(screen_pos.x + BODY_W * 0.5f,
                           screen_pos.y + TITLE_BAR_H + subtitle_offset + BODY_H * 0.5f);
        ImDrawList *dl = ImGui::GetWindowDrawList();

        // Render part number subtitle
        if (!node.part_number.empty()) {
            ImGui::TextDisabled("%s", node.part_number.c_str());
        }

        drawSchematicSymbol(dl, body_center, kind, color);
        ImGui::Dummy(ImVec2(BODY_W, BODY_H));

        for (size_t i = 0; i < node.input_pin_ids.size(); ++i) {
            ImNodes::BeginInputAttribute(node.input_pin_ids[i]);
            const char *label = (i < node.input_labels.size() && !node.input_labels[i].empty())
                                    ? node.input_labels[i].c_str()
                                    : "IN";
            ImGui::Text("%s", label);
            ImNodes::EndInputAttribute();
        }

        static const ImU32 probe_colors[4] = {
            IM_COL32(22, 199, 154, 255), // Teal
            IM_COL32(230, 150, 40, 255), // Orange
            IM_COL32(120, 50, 170, 255), // Purple
            IM_COL32(60, 140, 220, 255), // Blue
        };

        for (size_t i = 0; i < node.output_pin_ids.size(); ++i) {
            int pin = node.output_pin_ids[i];
            int slot = m_engine.probeSlotForPin(pin);
            if (slot >= 0) {
                ImNodes::PushColorStyle(ImNodesCol_Pin, probe_colors[slot]);
                ImNodes::PushColorStyle(ImNodesCol_PinHovered, probe_colors[slot]);
            }
            ImNodes::BeginOutputAttribute(pin);
            const char *label = (i < node.output_labels.size() && !node.output_labels[i].empty())
                                    ? node.output_labels[i].c_str()
                                    : "OUT";
            ImGui::Text("%s", label);
            ImNodes::EndOutputAttribute();
            if (slot >= 0) {
                ImNodes::PopColorStyle();
                ImNodes::PopColorStyle();
            }
        }

        ImNodes::PopColorStyle(); // NodeOutline
        ImNodes::PopColorStyle(); // TitleBar
        ImNodes::EndNode();
    }
}

void NodeGraphWidget::drawLinks() {
    std::unordered_set<int> hidden_nodes;
    for (const auto &g : m_engine.groups()) {
        if (g.collapsed) {
            for (int nid : g.member_node_ids)
                hidden_nodes.insert(nid);
        }
    }

    auto pin_owner_node = [this](int pin_id) -> int {
        for (const auto &n : m_engine.nodes()) {
            for (int p : n.input_pin_ids)
                if (p == pin_id)
                    return n.node_id;
            for (int p : n.output_pin_ids)
                if (p == pin_id)
                    return n.node_id;
        }
        return -1;
    };

    for (const auto &link : m_engine.links()) {
        int start_node = pin_owner_node(link.start_pin_id);
        int end_node = pin_owner_node(link.end_pin_id);
        if (start_node < 0 || end_node < 0)
            continue;

        bool start_hidden = hidden_nodes.count(start_node) > 0;
        bool end_hidden = hidden_nodes.count(end_node) > 0;
        if (start_hidden && end_hidden)
            continue; // internal link in collapsed group

        // When a link endpoint is on a hidden (grouped) node, redirect the link to the
        // group's synthesized boundary pin so it visually attaches to the collapsed block.
        // See spec: "links that exit the group are drawn from the boundary pin (collapsed)
        // or from the internal pin (expanded)".
        int draw_start_pin = link.start_pin_id;
        int draw_end_pin = link.end_pin_id;
        if (start_hidden) {
            auto it = m_real_to_synth_pin.find(link.start_pin_id);
            if (it != m_real_to_synth_pin.end()) {
                draw_start_pin = it->second;
            } else {
                continue; // no boundary pin for this internal pin; skip
            }
        }
        if (end_hidden) {
            auto it = m_real_to_synth_pin.find(link.end_pin_id);
            if (it != m_real_to_synth_pin.end()) {
                draw_end_pin = it->second;
            } else {
                continue;
            }
        }

        ImNodes::Link(link.link_id, draw_start_pin, draw_end_pin);
    }
}

void NodeGraphWidget::handleContextMenu(bool editor_hovered) {
    bool right_click = ImGui::IsMouseReleased(ImGuiMouseButton_Right);

    if (right_click && editor_hovered) {
        int hovered_node = -1;
        bool node_hovered = ImNodes::IsNodeHovered(&hovered_node);

        if (node_hovered) {
            if (hovered_node >= 50000 && hovered_node < 100000) {
                ImGui::OpenPopup("group_context_menu");
                m_context_menu_group_id = hovered_node;
            } else {
                ImGui::OpenPopup("node_context_menu");
                m_context_menu_node = hovered_node;
            }
        } else {
            // Capture mouse position in editor space for component placement
            ImVec2 mouse_screen = ImGui::GetMousePos();
            ImVec2 window_pos = ImGui::GetWindowPos();
            m_context_menu_pos =
                ImVec2(mouse_screen.x - window_pos.x, mouse_screen.y - window_pos.y);
            ImGui::OpenPopup("canvas_context_menu");
        }
    }

    // Render popups unconditionally so they stay open across frames
    if (ImGui::BeginPopup("group_context_menu")) {
        const Group *g = m_engine.groupById(m_context_menu_group_id);
        if (g) {
            if (ImGui::MenuItem(g->collapsed ? "Expand" : "Collapse")) {
                m_engine.setGroupCollapsed(m_context_menu_group_id, !g->collapsed);
            }
            if (ImGui::MenuItem("Rename")) {
                m_pending_rename_group_id = m_context_menu_group_id;
                std::strncpy(m_rename_buffer, g->name.c_str(), sizeof(m_rename_buffer) - 1);
                m_rename_buffer[sizeof(m_rename_buffer) - 1] = '\0';
            }
            if (ImGui::MenuItem("Ungroup")) {
                m_engine.removeGroup(m_context_menu_group_id);
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("node_context_menu")) {
        if (ImGui::MenuItem("Duplicate")) {
            if (onDuplicateNode)
                onDuplicateNode(m_context_menu_node);
        }
        if (ImGui::MenuItem("Remove")) {
            if (onRemoveNode)
                onRemoveNode(m_context_menu_node);
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("canvas_context_menu")) {
        for (const auto &addable : m_addable_components) {
            if (ImGui::MenuItem(addable.menu_label.c_str())) {
                if (addable.on_add)
                    addable.on_add(m_context_menu_pos);
            }
        }
        ImGui::EndPopup();
    }
}

void NodeGraphWidget::handleLinkCreation() {
    int start_pin, end_pin;
    m_link_created = ImNodes::IsLinkCreated(&start_pin, &end_pin);
    if (m_link_created) {
        // Translate synthesized boundary pin ids to real internal pin ids
        if (start_pin >= 100000) {
            auto it = m_synth_pin_to_real_pin.find(start_pin);
            if (it != m_synth_pin_to_real_pin.end())
                start_pin = it->second;
        }
        if (end_pin >= 100000) {
            auto it = m_synth_pin_to_real_pin.find(end_pin);
            if (it != m_synth_pin_to_real_pin.end())
                end_pin = it->second;
        }
        if (start_pin < 100000 && end_pin < 100000) {
            m_engine.addLink(start_pin, end_pin);

            // Find which node owns each pin
            int start_node = m_engine.nodeIdForPin(start_pin);
            int end_node = m_engine.nodeIdForPin(end_pin);

            // Rebuild boundary pins for any affected group
            for (const auto &g : m_engine.groups()) {
                for (int nid : g.member_node_ids) {
                    if (nid == start_node || nid == end_node) {
                        m_engine.rebuildGroupBoundaryPins(g.id);
                        break;
                    }
                }
            }

            if (onLinkChanged)
                onLinkChanged();
        }
    }
}

void NodeGraphWidget::handleLinkDeletion() {
    int link_id;
    if (ImNodes::IsLinkDestroyed(&link_id)) {
        m_engine.removeLink(link_id);
        // Rebuild boundary pins for all groups to reflect the removed link
        for (const auto &g : m_engine.groups()) {
            m_engine.rebuildGroupBoundaryPins(g.id);
        }
        if (onLinkChanged)
            onLinkChanged();
    }
}

void NodeGraphWidget::handleNodeDeletion() {
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        int num_selected = ImNodes::NumSelectedNodes();
        if (num_selected > 0) {
            std::vector<int> selected_nodes(num_selected);
            ImNodes::GetSelectedNodes(selected_nodes.data());
            for (int node_id : selected_nodes) {
                size_t links_before = m_engine.links().size();
                if (onRemoveNode)
                    onRemoveNode(node_id);
                if (m_engine.links().size() < links_before && onLinkChanged)
                    onLinkChanged();
                // Remove from position caches so they don't accumulate stale entries
                m_node_screen_positions.erase(node_id);
                m_last_node_grid_positions.erase(node_id);
            }
            ImNodes::ClearNodeSelection();
        }
    }
}

void NodeGraphWidget::handleProbeClick() {
    int hovered_pin = -1;
    int hovered_node = -1;

    // Detect click start on a pin or node
    if (ImGui::IsMouseClicked(0)) {
        ImVec2 pos = ImGui::GetMousePos();
        m_click_mouse_x = pos.x;
        m_click_mouse_y = pos.y;

        if (ImNodes::IsPinHovered(&hovered_pin)) {
            m_clicked_pin = hovered_pin;
        } else if (ImNodes::IsNodeHovered(&hovered_node)) {
            m_clicked_node = hovered_node;
        }
    }

    // On mouse release, if no link was created
    bool ctrl = ImGui::GetIO().KeyCtrl;
    bool shift = ImGui::GetIO().KeyShift;
    if (ImGui::IsMouseReleased(0) && (ctrl || shift)) {
        ImVec2 release_pos = ImGui::GetMousePos();
        float dx = release_pos.x - m_click_mouse_x;
        float dy = release_pos.y - m_click_mouse_y;
        bool is_click = std::sqrt(dx * dx + dy * dy) < 5.0f && !m_link_created;

        if (is_click) {
            int target_pin = -1;
            if (m_clicked_pin >= 0) {
                target_pin = m_clicked_pin;
            } else if (m_clicked_node >= 0) {
                for (const auto &node : m_engine.nodes()) {
                    if (node.node_id == m_clicked_node && !node.output_pin_ids.empty()) {
                        target_pin = node.output_pin_ids[0];
                        break;
                    }
                }
            }
            if (target_pin < 0 && m_clicked_node >= 50000 && m_clicked_node < 100000) {
                // Group-block click; probe the first output boundary pin
                const Group *g = m_engine.groupById(m_clicked_node);
                if (g) {
                    for (const auto &bp : g->boundary_pins) {
                        if (bp.is_output) {
                            target_pin = bp.internal_pin_id;
                            break;
                        }
                    }
                }
            }
            if (target_pin >= 100000) {
                // Synthesized boundary pin id; translate to real internal pin id
                auto it = m_synth_pin_to_real_pin.find(target_pin);
                if (it != m_synth_pin_to_real_pin.end()) {
                    target_pin = it->second;
                } else {
                    target_pin = -1; // stale pin id
                }
            }
            if (target_pin >= 0) {
                if (ctrl)
                    m_engine.addProbePin(target_pin);
                else if (shift)
                    m_engine.removeProbePin(target_pin);
            }
        }

        m_clicked_pin = -1;
        m_clicked_node = -1;
        m_link_created = false;
        return;
    }

    // Non-ctrl click: just reset state (no probe action, imnodes handles selection)
    if (ImGui::IsMouseReleased(0) || ImGui::IsMouseReleased(1)) {
        m_clicked_pin = -1;
        m_clicked_node = -1;
        m_link_created = false;
    }
}

void NodeGraphWidget::setupDarkTheme() {
    // ponytail: pushes are popped at the end of the same draw() call (balanced
    // before the closing ImGui::End()). Per-node title/border overrides are
    // pushed inside BeginNode/EndNode and explicitly popped in the same block.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(20, 20, 28, 255)); // editor window
    ImNodes::PushColorStyle(ImNodesCol_GridBackground, IM_COL32(30, 30, 38, 255));
    ImNodes::PushColorStyle(ImNodesCol_GridLine, IM_COL32(50, 50, 65, 100));
    ImNodes::PushColorStyle(ImNodesCol_GridLinePrimary, IM_COL32(55, 55, 72, 120));
    ImNodes::PushColorStyle(ImNodesCol_Link, IM_COL32(180, 180, 200, 200));
    ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(35, 35, 45, 255));
    ImNodes::PushColorStyle(ImNodesCol_NodeOutline, IM_COL32(80, 80, 100, 255));
    ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(60, 60, 80, 255));
    ImNodes::PushColorStyle(ImNodesCol_Pin, IM_COL32(200, 200, 220, 255));
    ImNodes::PushColorStyle(ImNodesCol_PinHovered, IM_COL32(120, 200, 255, 255));
}

size_t NodeGraphWidget::findNodeIndex(int node_id) const {
    (void)node_id;
    // Real implementation is added in a later task.
    return size_t(-1);
}
