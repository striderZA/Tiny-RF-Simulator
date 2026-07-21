#define IMGUI_DEFINE_MATH_OPERATORS
#include "node_graph_widget.h"
#include "logging_core.h"
#include "imgui.h"
#include "imnodes.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <limits>
#include <string>
#include <cmath>

NodeGraphWidget::NodeGraphWidget(NodeGraphEngine &engine) : m_engine(engine), m_context(nullptr) {
    m_context = ImNodes::EditorContextCreate();
    ImNodes::EditorContextSet(m_context);
}

NodeGraphWidget::~NodeGraphWidget() { ImNodes::EditorContextFree(m_context); }

void NodeGraphWidget::syncNodesFromEngine() {
    ImNodes::EditorContextSet(m_context);
    for (const auto& node : m_engine.nodes()) {
        // Register node with the imnodes pool if not already present.
        // SetNodeGridSpacePos uses ObjectPoolFindOrCreateObject internally,
        // so it creates the node if it doesn't exist (e.g. before first render frame)
        // and is a no-op for already-registered nodes beyond updating their Origin.
        ImNodes::SetNodeGridSpacePos(node.node_id, ImVec2(0, 0));
    }
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
            if (ImGui::BeginPopupModal("CreateSubcircuit", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
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
                    for (const auto& n : m_engine.nodes()) {
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
        ImNodes::PopColorStyle();   // PinHovered
        ImNodes::PopColorStyle();   // Pin
        ImNodes::PopColorStyle();   // TitleBar
        ImNodes::PopColorStyle();   // NodeOutline
        ImNodes::PopColorStyle();   // NodeBackground
        ImNodes::PopColorStyle();   // Link
        ImNodes::PopColorStyle();   // GridLinePrimary
        ImNodes::PopColorStyle();   // GridLine
        ImNodes::PopColorStyle();   // GridBackground
        ImGui::PopStyleColor();     // WindowBg
    }
    ImGui::End();
}

void NodeGraphWidget::drawNodes() {
    // Clear screen position cache - will be repopulated for nodes drawn this frame.
    // This ensures detectNodeMoves() only checks nodes that were actually drawn,
    // not stale entries from previous frames.
    m_node_screen_positions.clear();

    std::unordered_set<int> hidden_nodes;
    for (const auto& g : m_engine.groups()) {
        if (g.collapsed) {
            for (int nid : g.member_node_ids) hidden_nodes.insert(nid);
        }
    }

    bool first_visible = true;
    for (const auto &node : m_engine.nodes()) {
        if (hidden_nodes.count(node.node_id)) continue;

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
        const NodeKind kind = nodeKindFromLabel(node.label);
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
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Render part number subtitle
        if (!node.part_number.empty()) {
            ImGui::TextDisabled("%s", node.part_number.c_str());
        }

        drawSchematicSymbol(dl, body_center, kind, color);
        ImGui::Dummy(ImVec2(BODY_W, BODY_H));

        for (size_t i = 0; i < node.input_pin_ids.size(); ++i) {
            ImNodes::BeginInputAttribute(node.input_pin_ids[i]);
            const char* label = (i < node.input_labels.size() && !node.input_labels[i].empty())
                ? node.input_labels[i].c_str() : "IN";
            ImGui::Text("%s", label);
            ImNodes::EndInputAttribute();
        }

        static const ImU32 probe_colors[4] = {
            IM_COL32(22, 199, 154, 255),  // Teal
            IM_COL32(230, 150, 40, 255),  // Orange
            IM_COL32(120, 50, 170, 255),  // Purple
            IM_COL32(60, 140, 220, 255),  // Blue
        };

        for (size_t i = 0; i < node.output_pin_ids.size(); ++i) {
            int pin = node.output_pin_ids[i];
            int slot = m_engine.probeSlotForPin(pin);
            if (slot >= 0) {
                ImNodes::PushColorStyle(ImNodesCol_Pin, probe_colors[slot]);
                ImNodes::PushColorStyle(ImNodesCol_PinHovered, probe_colors[slot]);
            }
            ImNodes::BeginOutputAttribute(pin);
            const char* label = (i < node.output_labels.size() && !node.output_labels[i].empty())
                ? node.output_labels[i].c_str() : "OUT";
            ImGui::Text("%s", label);
            ImNodes::EndOutputAttribute();
            if (slot >= 0) {
                ImNodes::PopColorStyle();
                ImNodes::PopColorStyle();
            }
        }

        ImNodes::PopColorStyle();  // NodeOutline
        ImNodes::PopColorStyle();  // TitleBar
        ImNodes::EndNode();
    }
}

void NodeGraphWidget::drawLinks() {
    std::unordered_set<int> hidden_nodes;
    for (const auto& g : m_engine.groups()) {
        if (g.collapsed) {
            for (int nid : g.member_node_ids) hidden_nodes.insert(nid);
        }
    }

    auto pin_owner_node = [this](int pin_id) -> int {
        for (const auto& n : m_engine.nodes()) {
            for (int p : n.input_pin_ids) if (p == pin_id) return n.node_id;
            for (int p : n.output_pin_ids) if (p == pin_id) return n.node_id;
        }
        return -1;
    };

    for (const auto &link : m_engine.links()) {
        int start_node = pin_owner_node(link.start_pin_id);
        int end_node = pin_owner_node(link.end_pin_id);
        if (start_node < 0 || end_node < 0) continue;

        bool start_hidden = hidden_nodes.count(start_node) > 0;
        bool end_hidden = hidden_nodes.count(end_node) > 0;
        if (start_hidden && end_hidden) continue;  // internal link in collapsed group

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
                continue;  // no boundary pin for this internal pin; skip
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
            ImGui::OpenPopup("canvas_context_menu");
        }
    }

    // Render popups unconditionally so they stay open across frames
    if (ImGui::BeginPopup("group_context_menu")) {
        const Group* g = m_engine.groupById(m_context_menu_group_id);
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
            if (onDuplicateNode) onDuplicateNode(m_context_menu_node);
        }
        if (ImGui::MenuItem("Remove")) {
            if (onRemoveNode) onRemoveNode(m_context_menu_node);
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("canvas_context_menu")) {
        if (ImGui::MenuItem("Add Generator")) {
            if (onAddGenerator) onAddGenerator();
        }
        if (ImGui::MenuItem("Add Amplifier")) {
            if (onAddAmplifier) onAddAmplifier();
        }
        if (ImGui::MenuItem("Add Splitter")) {
            if (onAddSplitter) onAddSplitter();
        }
        if (ImGui::MenuItem("Add Combiner")) {
            if (onAddCombiner) onAddCombiner();
        }
        if (ImGui::MenuItem("Add Coax Cable")) {
            if (onAddCoaxCable) onAddCoaxCable();
        }
        if (ImGui::MenuItem("Add Equalizer")) {
            if (onAddEqualizer) onAddEqualizer();
        }
        if (ImGui::MenuItem("Add Mixer")) {
            if (onAddMixer) onAddMixer();
        }
        if (ImGui::MenuItem("Add RF ADC")) {
            if (onAddAdc) onAddAdc();
        }
        if (ImGui::MenuItem("Add PFB Channelizer")) {
            if (onAddPFB) onAddPFB();
        }
        if (ImGui::MenuItem("Add Ideal Filter")) {
            if (onAddIdealFilter) onAddIdealFilter();
        }
        if (ImGui::MenuItem("Add Attenuator")) {
            if (onAddAttenuator) onAddAttenuator();
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
            if (it != m_synth_pin_to_real_pin.end()) start_pin = it->second;
        }
        if (end_pin >= 100000) {
            auto it = m_synth_pin_to_real_pin.find(end_pin);
            if (it != m_synth_pin_to_real_pin.end()) end_pin = it->second;
        }
        if (start_pin < 100000 && end_pin < 100000) {
            m_engine.addLink(start_pin, end_pin);

            // Find which node owns each pin
            int start_node = m_engine.nodeIdForPin(start_pin);
            int end_node = m_engine.nodeIdForPin(end_pin);

            // Rebuild boundary pins for any affected group
            for (const auto& g : m_engine.groups()) {
                for (int nid : g.member_node_ids) {
                    if (nid == start_node || nid == end_node) {
                        m_engine.rebuildGroupBoundaryPins(g.id);
                        break;
                    }
                }
            }

            if (onLinkChanged) onLinkChanged();
        }
    }
}

void NodeGraphWidget::handleLinkDeletion() {
    int link_id;
    if (ImNodes::IsLinkDestroyed(&link_id)) {
        m_engine.removeLink(link_id);
        // Rebuild boundary pins for all groups to reflect the removed link
        for (const auto& g : m_engine.groups()) {
            m_engine.rebuildGroupBoundaryPins(g.id);
        }
        if (onLinkChanged) onLinkChanged();
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
                if (onRemoveNode) onRemoveNode(node_id);
                if (m_engine.links().size() < links_before && onLinkChanged) onLinkChanged();
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
                const Group* g = m_engine.groupById(m_clicked_node);
                if (g) {
                    for (const auto& bp : g->boundary_pins) {
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
                    target_pin = -1;  // stale pin id
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

namespace {

void showSpectrumTooltip(const Spectrum &spec, const char *direction) {
    ImGui::BeginTooltip();

    if (spec.frequencies.empty() && spec.tones.empty()) {
        ImGui::Text("%s: No signal", direction);
        ImGui::EndTooltip();
        return;
    }

    int num_tones = static_cast<int>(spec.tones.size());
    if (num_tones > 0) {
        double strongest_power = -std::numeric_limits<double>::infinity();
        double strongest_freq = 0.0;
        for (const auto &t : spec.tones) {
            if (t.power_dBm > strongest_power) {
                strongest_power = t.power_dBm;
                strongest_freq = t.freq_Hz;
            }
        }
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "Tones: %d  |  Strongest: %.3f MHz @ %.1f dBm",
                      num_tones, strongest_freq / 1e6, strongest_power);
        ImGui::TextUnformatted(buf);
    } else {
        ImGui::Text("Tones: 0");
    }

    if (!spec.noise_total_W.empty()) {
        double sum = 0.0;
        for (double n : spec.noise_total_W)
            sum += n;
        double avg_W = sum / static_cast<double>(spec.noise_total_W.size());
        double avg_dBm_per_Hz = 10.0 * std::log10(avg_W) + 30.0;
        ImGui::Text("Noise floor: %.1f dBm/Hz", avg_dBm_per_Hz);
    } else {
        ImGui::Text("Noise floor: -- dBm/Hz");
    }

    if (!spec.frequencies.empty()) {
        double f_min = spec.frequencies.front();
        double f_max = spec.frequencies.back();
        double f_center = (f_min + f_max) / 2.0;

        auto fmt_freq = [](double hz) -> std::string {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.3f MHz", hz / 1e6);
            return buf;
        };
        ImGui::Text("Freq range: %s - %s (center: %s)",
                    fmt_freq(f_min).c_str(), fmt_freq(f_max).c_str(),
                    fmt_freq(f_center).c_str());
    }

    ImGui::EndTooltip();
}

// ponytail: per-name symbol helpers are static and one-shot.

static void drawGeneratorSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    constexpr int N = 16;
    constexpr float W = 30.0f, H = 12.0f;
    ImVec2 pts[N];
    for (int i = 0; i < N; ++i) {
        float t = static_cast<float>(i) / (N - 1);
        float x = c.x - W + 2.0f * W * t;
        float y = c.y - H * std::sin(2.0f * 3.14159265f * 2.0f * t);
        pts[i] = ImVec2(x, y);
    }
    dl->AddPolyline(pts, N, color, ImDrawFlags_None, 2.0f);
}

static void drawAmplifierSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    ImVec2 a(c.x - 20, c.y - 16);
    ImVec2 b(c.x - 20, c.y + 16);
    ImVec2 d(c.x + 20, c.y);
    dl->AddTriangle(a, b, d, color, 2.0f);
    dl->AddLine(ImVec2(a.x - 8, c.y - 6), ImVec2(a.x - 8, c.y - 14), color, 2.0f);
    dl->AddLine(ImVec2(a.x - 12, c.y - 10), ImVec2(a.x - 4, c.y - 10), color, 2.0f);
    dl->AddLine(ImVec2(a.x - 8, c.y + 6), ImVec2(a.x - 8, c.y + 14), color, 2.0f);
}

static void drawMixerSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    dl->AddCircle(c, 14.0f, color, 24, 2.0f);
    dl->AddLine(ImVec2(c.x - 10, c.y - 10), ImVec2(c.x + 10, c.y + 10), color, 2.0f);
    dl->AddLine(ImVec2(c.x - 10, c.y + 10), ImVec2(c.x + 10, c.y - 10), color, 2.0f);
}

static void drawSplitterSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    ImVec2 in_pt(c.x - 22, c.y);
    ImVec2 mid(c.x, c.y);
    ImVec2 out_a(c.x + 22, c.y - 12);
    ImVec2 out_b(c.x + 22, c.y + 12);
    dl->AddLine(in_pt, mid, color, 2.0f);
    dl->AddLine(mid, out_a, color, 2.0f);
    dl->AddLine(mid, out_b, color, 2.0f);
    dl->AddCircleFilled(in_pt, 2.5f, color);
    dl->AddCircleFilled(out_a, 2.5f, color);
    dl->AddCircleFilled(out_b, 2.5f, color);
}

static void drawCombinerSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    // Reverse of splitter: 2 inputs on left, 1 output on right
    ImVec2 in_a(c.x - 22, c.y - 12);
    ImVec2 in_b(c.x - 22, c.y + 12);
    ImVec2 mid(c.x, c.y);
    ImVec2 out_pt(c.x + 22, c.y);
    dl->AddLine(in_a, mid, color, 2.0f);
    dl->AddLine(in_b, mid, color, 2.0f);
    dl->AddLine(mid, out_pt, color, 2.0f);
    dl->AddCircleFilled(in_a, 2.5f, color);
    dl->AddCircleFilled(in_b, 2.5f, color);
    dl->AddCircleFilled(out_pt, 2.5f, color);
}

static void drawAdcSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    ImVec2 pts[6] = {
        ImVec2(c.x - 24, c.y + 8),
        ImVec2(c.x - 16, c.y + 8),
        ImVec2(c.x - 16, c.y - 4),
        ImVec2(c.x - 8,  c.y - 4),
        ImVec2(c.x - 8,  c.y + 8),
        ImVec2(c.x + 24, c.y + 8),
    };
    dl->AddPolyline(pts, 6, color, ImDrawFlags_None, 2.0f);
}

static void drawFilterSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    dl->AddCircle(ImVec2(c.x - 10, c.y), 5.0f, color, 16, 2.0f);
    dl->AddCircle(ImVec2(c.x + 10, c.y), 5.0f, color, 16, 2.0f);
    dl->AddLine(ImVec2(c.x - 22, c.y - 10), ImVec2(c.x - 22, c.y + 10), color, 2.0f);
    dl->AddLine(ImVec2(c.x + 22, c.y - 10), ImVec2(c.x + 22, c.y + 10), color, 2.0f);
    dl->AddLine(ImVec2(c.x - 22, c.y), ImVec2(c.x + 22, c.y), color, 2.0f);
}

static void drawCoaxSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    dl->AddCircle(c, 16.0f, color, 32, 2.0f);
    dl->AddCircle(c,  8.0f, color, 24, 2.0f);
}

static void drawEqualizerSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    // Rising/falling slope line
    dl->AddLine(ImVec2(c.x - 20, c.y + 8), ImVec2(c.x + 20, c.y - 8), color, 2.0f);
    // Small reference markers
    dl->AddCircleFilled(ImVec2(c.x - 14, c.y + 4), 2.0f, color);
    dl->AddCircleFilled(ImVec2(c.x + 14, c.y - 4), 2.0f, color);
}

static void drawAttenuatorSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    // Zigzag resistor-style symbol with "ATT" label
    const float x0 = c.x - 15.0f;
    const float x1 = c.x + 15.0f;
    const float amp = 4.0f;
    dl->PathClear();
    dl->PathLineTo(ImVec2(x0, c.y));
    for (int i = 0; i < 4; ++i) {
        float x = x0 + (x1 - x0) * (i + 0.5f) / 4.0f;
        float yo = (i % 2 == 0) ? -amp : amp;
        dl->PathLineTo(ImVec2(x, c.y + yo));
    }
    dl->PathLineTo(ImVec2(x1, c.y));
    dl->PathStroke(color, 0, 2.0f);
    const char* label = "ATT";
    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - 14.0f), color, label);
}

static void drawPfbSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    for (int i = 0; i < 3; ++i) {
        float y_off = (i - 1) * 8.0f;
        dl->AddRect(
            ImVec2(c.x - 20, c.y - 4 + y_off),
            ImVec2(c.x + 20, c.y + 4 + y_off),
            color, 0.0f, ImDrawFlags_None, 2.0f);
    }
    dl->AddText(ImVec2(c.x - 4, c.y - 18), color, "M");
}

static void drawGroupCollapsedSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    ImVec2 a(c.x - 18, c.y - 6);
    ImVec2 b(c.x, c.y);
    ImVec2 d(c.x + 18, c.y + 6);
    dl->AddLine(a, b, color, 2.0f);
    dl->AddLine(b, d, color, 2.0f);
    dl->AddCircleFilled(a, 3.0f, color);
    dl->AddCircleFilled(b, 3.0f, color);
    dl->AddCircleFilled(d, 3.0f, color);
}
}

void NodeGraphWidget::drawSchematicSymbol(ImDrawList* dl, ImVec2 center, NodeKind kind, ImU32 color) {
    switch (kind) {
        case NodeKind::Generator:      drawGeneratorSymbol(dl, center, color);      break;
        case NodeKind::Amplifier:      drawAmplifierSymbol(dl, center, color);      break;
        case NodeKind::Splitter:       drawSplitterSymbol(dl, center, color);       break;
        case NodeKind::Mixer:          drawMixerSymbol(dl, center, color);          break;
        case NodeKind::Adc:            drawAdcSymbol(dl, center, color);            break;
        case NodeKind::PFB:            drawPfbSymbol(dl, center, color);            break;
        case NodeKind::IdealFilter:    drawFilterSymbol(dl, center, color);         break;
        case NodeKind::CoaxCable:      drawCoaxSymbol(dl, center, color);           break;
        case NodeKind::Equalizer:      drawEqualizerSymbol(dl, center, color);      break;
        case NodeKind::Attenuator:     drawAttenuatorSymbol(dl, center, color);     break;
        case NodeKind::Combiner:       drawCombinerSymbol(dl, center, color);       break;
        case NodeKind::GroupCollapsed: drawGroupCollapsedSymbol(dl, center, color); break;
        default:
            // Future/unknown kinds: draw nothing silently.
            break;
    }
}

void NodeGraphWidget::setupDarkTheme() {
    // ponytail: pushes are popped at the end of the same draw() call (balanced
    // before the closing ImGui::End()). Per-node title/border overrides are
    // pushed inside BeginNode/EndNode and explicitly popped in the same block.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(20, 20, 28, 255));  // editor window
    ImNodes::PushColorStyle(ImNodesCol_GridBackground,  IM_COL32(30, 30, 38, 255));
    ImNodes::PushColorStyle(ImNodesCol_GridLine,        IM_COL32(50, 50, 65, 100));
    ImNodes::PushColorStyle(ImNodesCol_GridLinePrimary, IM_COL32(55, 55, 72, 120));
    ImNodes::PushColorStyle(ImNodesCol_Link,            IM_COL32(180, 180, 200, 200));
    ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(35, 35, 45, 255));
    ImNodes::PushColorStyle(ImNodesCol_NodeOutline,     IM_COL32(80, 80, 100, 255));
    ImNodes::PushColorStyle(ImNodesCol_TitleBar,        IM_COL32(60, 60, 80, 255));
    ImNodes::PushColorStyle(ImNodesCol_Pin,             IM_COL32(200, 200, 220, 255));
    ImNodes::PushColorStyle(ImNodesCol_PinHovered,      IM_COL32(120, 200, 255, 255));
}

void NodeGraphWidget::rebuildSynthMaps() {
    m_synth_pin_to_real_pin.clear();
    m_real_to_synth_pin.clear();
    for (const auto& g : m_engine.groups()) {
        // Only COLLAPSED groups have registered synthetic pins; expanded groups'
        // boundary pins are cleared (by setGroupCollapsed), but guard anyway.
        if (!g.collapsed) continue;
        for (const auto& bp : g.boundary_pins) {
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
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (const auto& g : m_engine.groups()) {
        if (g.collapsed) continue;
        if (g.member_node_ids.empty()) continue;

        // Compute bounding box in screen space from cached screen-space positions
        ImVec2 top_left(std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max());
        ImVec2 bottom_right(std::numeric_limits<float>::lowest(),
                            std::numeric_limits<float>::lowest());
        const float NODE_W = 120.0f, NODE_H = 80.0f;  // approximate; refined in spike
        for (int nid : g.member_node_ids) {
            auto pos_it = m_node_screen_positions.find(nid);
            if (pos_it == m_node_screen_positions.end()) continue;
            ImVec2 pos = pos_it->second;
            top_left.x = std::min(top_left.x, pos.x);
            top_left.y = std::min(top_left.y, pos.y);
            bottom_right.x = std::max(bottom_right.x, pos.x + NODE_W);
            bottom_right.y = std::max(bottom_right.y, pos.y + NODE_H);
        }
        if (top_left.x > bottom_right.x) continue;  // no valid members

        ImVec2 pad(16, 16);
        ImVec2 tl_screen = top_left - pad;
        ImVec2 br_screen = bottom_right + pad;

        dl->AddRectFilled(tl_screen, br_screen, IM_COL32(80, 80, 120, 24));
        dl->AddRect(tl_screen, br_screen, IM_COL32(120, 120, 180, 96));

        // Title bar at the top of the rectangle (in screen space)
        drawGroupTitleBar(const_cast<Group&>(g), top_left);
    }
}

void NodeGraphWidget::drawGroupCollapsedBlocks() {
    for (const auto& g : m_engine.groups()) {
        if (!g.collapsed) continue;

        // Compute centroid in screen space using cached positions (member nodes may have been
        // removed from the imnodes pool since hidden nodes aren't rendered each frame)
        if (g.member_node_ids.empty()) continue;
        ImVec2 sum(0, 0);
        int count = 0;
        for (int nid : g.member_node_ids) {
            auto pos_it = m_node_screen_positions.find(nid);
            if (pos_it == m_node_screen_positions.end()) continue;
            sum.x += pos_it->second.x;
            sum.y += pos_it->second.y;
            ++count;
        }
        if (count == 0) continue;
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
            for (const auto& bp : g.boundary_pins) {
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
        ImDrawList* dl = ImGui::GetWindowDrawList();
        drawSchematicSymbol(dl, body_center, NodeKind::GroupCollapsed, group_color);

        // Expand button in the body. Placing it in the title bar would conflict with
        // imnodes' title-bar drag handling. Right-click context menu still works as a fallback.
        ImGui::Dummy(ImVec2(0, 4));  // small vertical spacer
        if (ImGui::Button("Expand", ImVec2(120, 0))) {
            m_engine.setGroupCollapsed(g.id, false);
        }

        ImNodes::PopColorStyle();  // NodeOutline
        ImNodes::PopColorStyle();  // TitleBar
        ImNodes::EndNode();
    }
}

void NodeGraphWidget::drawGroupTitleBar(Group& g, const ImVec2& top_left_screen) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
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
    bool btn_hovered = mouse.x >= btn_min.x && mouse.x <= btn_max.x &&
                       mouse.y >= btn_min.y && mouse.y <= btn_max.y;
    ImU32 btn_color = btn_hovered ? IM_COL32(130, 130, 180, 255) : IM_COL32(100, 100, 140, 255);
    dl->AddRectFilled(btn_min, btn_max, btn_color);
    // Center the glyph in the 24x24 button
    ImVec2 text_pos(btn_min.x + 8, btn_min.y + 4);
    dl->AddText(text_pos, IM_COL32(255, 255, 255, 255), "\xE2\x96\xBC");  // ▼

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
    for (const auto& node : m_engine.nodes()) {
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
        ImDrawList* dl = ImGui::GetWindowDrawList();
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

        ImVec2 screen_a(
            std::min(m_rubber_band_start.x, m_rubber_band_end.x),
            std::min(m_rubber_band_start.y, m_rubber_band_end.y));
        ImVec2 screen_b(
            std::max(m_rubber_band_start.x, m_rubber_band_end.x),
            std::max(m_rubber_band_start.y, m_rubber_band_end.y));

        for (const auto& node : m_engine.nodes()) {
            if (m_engine.groupIdForNode(node.node_id) != -1) continue;  // skip grouped
            auto pos_it = m_node_screen_positions.find(node.node_id);
            if (pos_it == m_node_screen_positions.end()) continue;
            ImVec2 pos = pos_it->second;
            ImVec2 center = pos + ImVec2(60, 40);  // approximate node center
            if (center.x >= screen_a.x && center.x <= screen_b.x &&
                center.y >= screen_a.y && center.y <= screen_b.y) {
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

size_t NodeGraphWidget::findNodeIndex(int node_id) const {
    (void)node_id;
    // Real implementation is added in a later task.
    return size_t(-1);
}

void NodeGraphWidget::showPinTooltips() {
    int hovered_pin;
    if (!ImNodes::IsPinHovered(&hovered_pin))
        return;

    for (const auto &node : m_engine.nodes()) {
        const auto *signal = node.signal_node;
        if (!signal)
            continue;

        for (size_t i = 0; i < node.input_pin_ids.size(); ++i) {
            if (node.input_pin_ids[i] != hovered_pin)
                continue;
            const Spectrum *spec = (i < signal->inputs.size())
                                       ? signal->inputs[i]
                                       : nullptr;
            if (spec) {
                showSpectrumTooltip(*spec, "IN");
                return;
            }
        }

        for (size_t i = 0; i < node.output_pin_ids.size(); ++i) {
            if (node.output_pin_ids[i] != hovered_pin)
                continue;
            const Spectrum &spec = (i < signal->outputs.size())
                                       ? signal->outputs[i]
                                       : Spectrum();
            showSpectrumTooltip(spec, "OUT");
            return;
        }
    }

    // Boundary pin tooltips (synthesized pins with ids >= 100000)
    if (hovered_pin >= 100000) {
        auto it = m_synth_pin_to_real_pin.find(hovered_pin);
        if (it != m_synth_pin_to_real_pin.end()) {
            int real_pin = it->second;
            for (const auto& node : m_engine.nodes()) {
                for (size_t i = 0; i < node.output_pin_ids.size(); ++i) {
                    if (node.output_pin_ids[i] == real_pin && node.signal_node) {
                        if (i < node.signal_node->outputs.size()) {
                            showSpectrumTooltip(node.signal_node->outputs[i], "OUT");
                            return;
                        }
                    }
                }
                for (size_t i = 0; i < node.input_pin_ids.size(); ++i) {
                    if (node.input_pin_ids[i] == real_pin && node.signal_node) {
                        const Spectrum* spec = (i < node.signal_node->inputs.size())
                            ? node.signal_node->inputs[i] : nullptr;
                        if (spec) {
                            showSpectrumTooltip(*spec, "IN");
                            return;
                        }
                    }
                }
            }
        }
    }
}

void NodeGraphWidget::showNodeHoverTooltips() {
    if (!onNodeHover) return;

    for (const auto &node : m_engine.nodes()) {
        int hovered_node = -1;
        if (ImNodes::IsNodeHovered(&hovered_node) && hovered_node == node.node_id) {
            std::string summary = onNodeHover(node.node_id);
            if (!summary.empty()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted(summary.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            break;
        }
    }
}
