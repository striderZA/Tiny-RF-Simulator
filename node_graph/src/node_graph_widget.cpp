#define IMGUI_DEFINE_MATH_OPERATORS
#include "node_graph_widget.h"
#include "imgui.h"
#include "imnodes.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

NodeGraphWidget::NodeGraphWidget(const NodeGraphEngine &engine, NodeGraphWidgetActions actions)
    : m_engine(engine), m_actions(std::move(actions)), m_context(nullptr) {
    m_context = ImNodes::EditorContextCreate();
    ImNodes::EditorContextSet(m_context);
}

NodeGraphWidget::~NodeGraphWidget() { ImNodes::EditorContextFree(m_context); }

bool NodeGraphWidget::setNodeEditorSpacePosition(int node_id, ImVec2 position) {
    ImNodes::EditorContextSet(m_context);
    const ImVec2 pan = ImNodes::EditorContextGetPanning();
    const ImVec2 desired_grid = position - pan;
    const bool registered = m_registered_in_pool.count(node_id) != 0;
    const bool hidden = m_hidden_last_frame.count(node_id) != 0;

    ImVec2 current_grid(0.0f, 0.0f);
    const auto cached_grid = m_cached_grid_positions.find(node_id);
    if (registered && !hidden)
        current_grid = ImNodes::GetNodeGridSpacePos(node_id);
    else if (cached_grid != m_cached_grid_positions.end())
        current_grid = cached_grid->second;

    const auto same_coordinate = [](float current, float desired, float canvas_pan,
                                    float editor_position) {
        if (current == desired)
            return true;
        if (!std::isfinite(current) || !std::isfinite(desired) || !std::isfinite(canvas_pan) ||
            !std::isfinite(editor_position))
            return false;
        const float scale = std::max({1.0f, std::abs(canvas_pan), std::abs(editor_position)});
        return std::abs(current - desired) <= std::numeric_limits<float>::epsilon() * scale;
    };
    const bool unchanged = same_coordinate(current_grid.x, desired_grid.x, pan.x, position.x) &&
                           same_coordinate(current_grid.y, desired_grid.y, pan.y, position.y);
    const bool needs_registration = !registered && !hidden;
    if (unchanged && !needs_registration) {
        if (registered && !hidden)
            m_cached_grid_positions[node_id] = current_grid;
        return false;
    }

    // Also register a new node whose desired position is already the origin.
    ImNodes::SetNodeEditorSpacePos(node_id, position);
    m_registered_in_pool.insert(node_id);
    const ImVec2 actual_editor_position = ImNodes::GetNodeEditorSpacePos(node_id);
    m_last_node_grid_positions[node_id] = actual_editor_position;
    m_prev_node_grid_positions[node_id] = actual_editor_position;
    m_cached_grid_positions[node_id] = ImNodes::GetNodeGridSpacePos(node_id);
    return !unchanged;
}

void NodeGraphWidget::selectNode(int node_id) {
    ImNodes::EditorContextSet(m_context);
    ImNodes::ClearNodeSelection();
    ImNodes::SelectNode(node_id);
}

std::optional<NodeGraphBounds>
NodeGraphWidget::nodeBoundsExcluding(const std::unordered_set<int> &excluded_node_ids) const {
    const ImVec2 kFallbackNodeDimensions(260.0f, 160.0f);
    std::optional<NodeGraphBounds> bounds;
    const auto include_bounds = [&bounds](const NodeGraphBounds &candidate) {
        if (!bounds) {
            bounds = candidate;
            return;
        }
        bounds->min.x = std::min(bounds->min.x, candidate.min.x);
        bounds->min.y = std::min(bounds->min.y, candidate.min.y);
        bounds->max.x = std::max(bounds->max.x, candidate.max.x);
        bounds->max.y = std::max(bounds->max.y, candidate.max.y);
    };

    for (const auto &node : m_engine.nodes()) {
        if (excluded_node_ids.count(node.node_id))
            continue;

        const auto position_it = m_last_node_grid_positions.find(node.node_id);
        const ImVec2 position = position_it != m_last_node_grid_positions.end()
                                    ? position_it->second
                                    : ImVec2(0.0f, 0.0f);
        const auto dimensions_it = m_cached_node_dimensions.find(node.node_id);
        const ImVec2 dimensions = dimensions_it != m_cached_node_dimensions.end()
                                      ? dimensions_it->second
                                      : kFallbackNodeDimensions;
        include_bounds({position, position + dimensions});
    }

    for (const auto &group : m_engine.groups()) {
        if (!group.collapsed || !m_rendered_collapsed_groups.count(group.id))
            continue;
        const auto bounds_it = m_cached_collapsed_group_bounds.find(group.id);
        if (bounds_it != m_cached_collapsed_group_bounds.end())
            include_bounds(bounds_it->second);
    }
    return bounds;
}

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
            const ImVec2 editor_position = ImNodes::GetNodeEditorSpacePos(node.node_id);
            m_cached_grid_positions[node.node_id] = ImNodes::GetNodeGridSpacePos(node.node_id);
            m_last_node_grid_positions[node.node_id] = editor_position;
            m_prev_node_grid_positions[node.node_id] = editor_position;
        }
    }
}

void NodeGraphWidget::captureGridPositions() {
    ImNodes::EditorContextSet(m_context);
    m_cached_grid_positions.clear();
    for (const auto &node : m_engine.nodes()) {
        m_cached_grid_positions[node.node_id] = ImNodes::GetNodeGridSpacePos(node.node_id);
        const ImVec2 editor_position = ImNodes::GetNodeEditorSpacePos(node.node_id);
        m_last_node_grid_positions[node.node_id] = editor_position;
        m_prev_node_grid_positions[node.node_id] = editor_position;
    }
}

void NodeGraphWidget::markNodesRegistered() {
    for (const auto &node : m_engine.nodes()) {
        m_registered_in_pool.insert(node.node_id);
    }
}

ImVec2 NodeGraphWidget::editorPositionForSave(int node_id) {
    ImNodes::EditorContextSet(m_context);
    if (!m_registered_in_pool.count(node_id) || m_hidden_last_frame.count(node_id)) {
        // New GUI-created nodes may not have reached BeginNode yet. Collapsed-group members
        // are also absent from the pool. Use the last known grid position, or origin for a
        // never-placed node, then apply the current pan to produce editor-space coordinates.
        const auto grid_it = m_cached_grid_positions.find(node_id);
        const ImVec2 grid =
            grid_it != m_cached_grid_positions.end() ? grid_it->second : ImVec2(0, 0);
        return grid + ImNodes::EditorContextGetPanning();
    }
    return ImNodes::GetNodeEditorSpacePos(node_id);
}

void NodeGraphWidget::draw(const char *title, bool *p_open) {
    ImNodes::EditorContextSet(m_context);

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

        // Hover tooltips (after EndNodeEditor per imnodes query pattern)
        showPinTooltips();
        showNodeHoverTooltips();
        showLinkTooltips();

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
                    if (m_actions.createGroup)
                        m_actions.createGroup(name_buf, m_rubber_band_members);
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
    // Longest matching prefix wins, so an overlapping pair such as
    // "SPDT Switch" / "SPDT Switch (2:1)" cannot depend on registration order.
    NodeKind best = NodeKind::Unknown;
    size_t best_len = 0;
    for (const auto &[prefix, kind] : m_kind_prefixes) {
        if (prefix.size() > best_len && label.rfind(prefix, 0) == 0) {
            best = kind;
            best_len = prefix.size();
        }
    }
    return best;
}

namespace {

float textWidth(std::string_view text) {
    return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
}

// Trims `text` with a trailing "..." so it fits `max_width`; unchanged when it already fits.
// Cuts only at UTF-8 character starts, so a multi-byte character is never split.
std::string fitTitleToWidth(const std::string &text, float max_width) {
    if (textWidth(text) <= max_width)
        return text;
    constexpr std::string_view ellipsis = "...";
    std::string candidate;
    for (size_t keep = text.size(); keep-- > 0;) {
        if ((static_cast<unsigned char>(text[keep]) & 0xC0) == 0x80)
            continue; // continuation byte, not a cut point
        candidate.assign(text, 0, keep);
        candidate += ellipsis;
        if (textWidth(candidate) <= max_width)
            return candidate;
    }
    return std::string(ellipsis);
}

// Label for pin `index`, or `fallback` when the node sets no label for it.
std::string_view pinLabel(const std::vector<std::string> &labels, size_t index,
                          std::string_view fallback) {
    if (index < labels.size() && !labels[index].empty())
        return labels[index];
    return fallback;
}

} // namespace

void NodeGraphWidget::drawNodes() {
    // Clear screen position cache - will be repopulated for nodes drawn this frame.
    // This ensures detectNodeMoves() only checks nodes that were actually drawn,
    // not stale entries from previous frames.
    m_node_screen_positions.clear();
    const std::unordered_set<int> previously_hidden = m_hidden_last_frame;
    std::unordered_set<int> hidden_nodes;
    for (const auto &g : m_engine.groups()) {
        if (g.collapsed) {
            for (int nid : g.member_node_ids)
                hidden_nodes.insert(nid);
        }
    }
    m_hidden_last_frame = hidden_nodes;

    bool first_visible = true;
    for (const auto &node : m_engine.nodes()) {
        if (hidden_nodes.count(node.node_id))
            continue;

        const bool was_hidden = previously_hidden.count(node.node_id) > 0;
        if (was_hidden) {
            // ImNodes drops hidden members at EndNodeEditor. Restore the cached grid position
            // before their first BeginNode after expansion.
            const auto cached_position = m_cached_grid_positions.find(node.node_id);
            if (cached_position != m_cached_grid_positions.end())
                ImNodes::SetNodeGridSpacePos(node.node_id, cached_position->second);
        }

        const NodeKind kind = kindForLabel(node.label);
        const ImU32 color = static_cast<ImU32>(themeColor(kind));
        // ImNodes copies TitleBar and NodeOutline in BeginNode, so push them first.
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, color);
        ImNodes::PushColorStyle(ImNodesCol_NodeOutline, color);
        ImNodes::BeginNode(node.node_id);
        m_registered_in_pool.insert(node.node_id);
        // A cache restore is not a user drag; reset both movement baselines.
        if (was_hidden) {
            const ImVec2 editor_position = ImNodes::GetNodeEditorSpacePos(node.node_id);
            m_last_node_grid_positions[node.node_id] = editor_position;
            m_prev_node_grid_positions[node.node_id] = editor_position;
        }

        // Cache screen-space positions after BeginNode registers the node in the imnodes
        // pool, so GetNodeScreenSpacePos/GetNodeGridSpacePos don't assert on first frame.
        // drawGroupBackgrounds and drawGroupCollapsedBlocks use this cache because hidden
        // nodes get removed from the pool by ObjectPoolUpdate in EndNodeEditor.
        ImVec2 screen_pos = ImNodes::GetNodeScreenSpacePos(node.node_id);
        m_node_screen_positions[node.node_id] = screen_pos;
        // Remember the pan-independent grid position too. Collapsed-group
        // members stop being drawn (and are dropped from the imnodes pool), so
        // this cache is what lets drawGroupCollapsedBlocks() place their block
        // (issue #116).
        ImVec2 grid_pos = ImNodes::GetNodeGridSpacePos(node.node_id);
        m_cached_grid_positions[node.node_id] = grid_pos;
        if (first_visible) {
            // Refresh the grid-to-screen offset from the first visible node every frame
            // so it's always current with panning changes.
            m_grid_to_screen_offset = screen_pos - grid_pos;
            first_visible = false;
        }
        // The body is a fixed 96x64 box. The title is clamped to its width, so a long label
        // cannot widen the node past the body plus node padding.
        constexpr float BODY_W = 96.0f;
        constexpr float BODY_H = 64.0f;
        ImNodes::BeginNodeTitleBar();
        const std::string title = fitTitleToWidth(node.label, BODY_W);
        ImGui::TextUnformatted(title.c_str());
        ImNodes::EndNodeTitleBar();

        // Render part number subtitle
        if (!node.part_number.empty()) {
            ImGui::TextDisabled("%s", node.part_number.c_str());
        }

        // Schematic symbol, centred on the body Dummy below. The cursor is the Dummy's top-left
        // once the title bar and part-number line are placed, so no offsets are assumed.
        const ImVec2 body_origin = ImGui::GetCursorScreenPos();
        const float content_left = ImGui::GetCursorPosX();
        const ImVec2 body_center(body_origin.x + BODY_W * 0.5f, body_origin.y + BODY_H * 0.5f);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        drawSchematicSymbol(dl, body_center, kind, color);
        ImGui::Dummy(ImVec2(BODY_W, BODY_H));

        // Pin rows: row r holds input r on the left and output r on the right, so a node is
        // max(inputs, outputs) rows tall instead of inputs + outputs.
        static const ImU32 probe_colors[4] = {
            IM_COL32(22, 199, 154, 255), // Teal
            IM_COL32(230, 150, 40, 255), // Orange
            IM_COL32(120, 50, 170, 255), // Purple
            IM_COL32(60, 140, 220, 255), // Blue
        };
        const float pin_gap = ImGui::GetStyle().ItemSpacing.x;
        const size_t input_count = node.input_pin_ids.size();
        const size_t output_count = node.output_pin_ids.size();
        for (size_t row = 0; row < std::max(input_count, output_count); ++row) {
            float input_end_x = content_left;
            if (row < input_count) {
                const std::string_view label = pinLabel(node.input_labels, row, "IN");
                ImNodes::BeginInputAttribute(node.input_pin_ids[row]);
                ImGui::TextUnformatted(label.data(), label.data() + label.size());
                ImNodes::EndInputAttribute();
                input_end_x = content_left + textWidth(label);
            }
            if (row < output_count) {
                const std::string_view label = pinLabel(node.output_labels, row, "OUT");
                // Right-align on the body edge, but never over this row's input label.
                float output_x = content_left + BODY_W - textWidth(label);
                if (row < input_count) {
                    ImGui::SameLine();
                    output_x = std::max(output_x, input_end_x + pin_gap);
                }
                ImGui::SetCursorPosX(output_x);
                const int pin = node.output_pin_ids[row];
                const int slot = m_engine.probeSlotForPin(pin);
                if (slot >= 0) {
                    ImNodes::PushColorStyle(ImNodesCol_Pin, probe_colors[slot]);
                    ImNodes::PushColorStyle(ImNodesCol_PinHovered, probe_colors[slot]);
                }
                ImNodes::BeginOutputAttribute(pin);
                ImGui::TextUnformatted(label.data(), label.data() + label.size());
                ImNodes::EndOutputAttribute();
                if (slot >= 0) {
                    ImNodes::PopColorStyle();
                    ImNodes::PopColorStyle();
                }
            }
        }

        ImNodes::EndNode();
        ImNodes::PopColorStyle(); // NodeOutline
        ImNodes::PopColorStyle(); // TitleBar
        m_cached_node_dimensions[node.node_id] = ImNodes::GetNodeDimensions(node.node_id);
    }
}

void NodeGraphWidget::drawLinks() {
    m_cross_group_links_drawn = 0;
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
        if (start_hidden && end_hidden &&
            m_engine.groupIdForNode(start_node) == m_engine.groupIdForNode(end_node)) {
            continue; // both endpoints inside the same collapsed group
        }

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

        if (start_hidden && end_hidden)
            ++m_cross_group_links_drawn;
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
        } else if (ImNodes::IsLinkHovered(&m_context_menu_link_id)) {
            ImGui::OpenPopup("link_context_menu");
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
                if (m_actions.setGroupCollapsed)
                    m_actions.setGroupCollapsed(m_context_menu_group_id, !g->collapsed);
            }
            if (ImGui::MenuItem("Rename")) {
                m_pending_rename_group_id = m_context_menu_group_id;
                std::strncpy(m_rename_buffer, g->name.c_str(), sizeof(m_rename_buffer) - 1);
                m_rename_buffer[sizeof(m_rename_buffer) - 1] = '\0';
                ImGui::OpenPopup("RenameSubcircuit");
            }
            if (ImGui::MenuItem("Ungroup")) {
                if (m_actions.removeGroup)
                    m_actions.removeGroup(m_context_menu_group_id);
            }
        }
        ImGui::EndPopup();
    }
    if (m_pending_rename_group_id >= 0 &&
        ImGui::BeginPopupModal("RenameSubcircuit", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!m_engine.groupById(m_pending_rename_group_id)) {
            m_pending_rename_group_id = -1;
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::InputText("Name", m_rename_buffer, sizeof(m_rename_buffer));
            if (ImGui::Button("Rename")) {
                if (m_actions.renameGroup)
                    m_actions.renameGroup(m_pending_rename_group_id, m_rename_buffer);
                m_pending_rename_group_id = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                m_pending_rename_group_id = -1;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("node_context_menu")) {
        if (ImGui::MenuItem("Duplicate")) {
            if (m_actions.duplicateComponent)
                m_actions.duplicateComponent(m_context_menu_node);
        }
        if (ImGui::MenuItem("Remove")) {
            if (m_actions.removeComponent)
                m_actions.removeComponent(m_context_menu_node);
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("link_context_menu")) {
        if (ImGui::MenuItem("Remove"))
            removeLink(m_context_menu_link_id);
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
            if (m_actions.connectLink)
                m_actions.connectLink(start_pin, end_pin);
        }
    }
}

void NodeGraphWidget::handleLinkDeletion() {
    int link_id;
    if (ImNodes::IsLinkDestroyed(&link_id))
        removeLink(link_id);
}

void NodeGraphWidget::removeLink(int link_id) {
    if (m_actions.disconnectLink)
        m_actions.disconnectLink(link_id);
}

void NodeGraphWidget::handleNodeDeletion() {
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        // Deleting selected links takes priority over nodes (imnodes clears the
        // other selection on click, but both may be selected programmatically).
        int num_selected_links = ImNodes::NumSelectedLinks();
        if (num_selected_links > 0) {
            std::vector<int> selected_links(num_selected_links);
            ImNodes::GetSelectedLinks(selected_links.data());
            for (int link_id : selected_links)
                removeLink(link_id);
            ImNodes::ClearLinkSelection();
        }

        int num_selected = ImNodes::NumSelectedNodes();
        if (num_selected > 0) {
            std::vector<int> selected_nodes(num_selected);
            ImNodes::GetSelectedNodes(selected_nodes.data());
            for (int node_id : selected_nodes) {
                if (m_actions.removeComponent)
                    m_actions.removeComponent(node_id);
                // Remove from position caches so they don't accumulate stale entries
                m_node_screen_positions.erase(node_id);
                m_cached_grid_positions.erase(node_id);
                m_last_node_grid_positions.erase(node_id);
                m_prev_node_grid_positions.erase(node_id);
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
                // Group-block click; probe the first output boundary pin — the same
                // pin the collapsed block's tooltip hint is gated on.
                target_pin = m_engine.firstOutputBoundaryPin(m_clicked_node);
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
                if (ctrl) {
                    if (m_actions.addProbePin)
                        m_actions.addProbePin(target_pin);
                } else if (shift && m_actions.removeProbePin) {
                    m_actions.removeProbePin(target_pin);
                }
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
