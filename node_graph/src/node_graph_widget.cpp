#define IMGUI_DEFINE_MATH_OPERATORS
#include "node_graph_widget.h"
#include "imgui.h"
#include "imnodes.h"
#include <algorithm>
#include <cstdio>
#include <limits>
#include <string>

NodeGraphWidget::NodeGraphWidget(NodeGraphEngine &engine) : m_engine(engine), m_context(nullptr) {
    m_context = ImNodes::EditorContextCreate();
    ImNodes::EditorContextSet(m_context);
}

NodeGraphWidget::~NodeGraphWidget() { ImNodes::EditorContextFree(m_context); }

void NodeGraphWidget::draw(const char *title, bool *p_open) {
    ImNodes::EditorContextSet(m_context);

    if (ImGui::Begin(title, p_open)) {
        ImNodes::ClearNodeSelection();
        rebuildSynthMaps();

        drawGroupBackgrounds();
        drawPhantomNodes();

        ImNodes::BeginNodeEditor();

        drawNodes();
        drawGroupCollapsedBlocks();
        drawLinks();

        // Cache editor hover state before EndNodeEditor (IsEditorHovered only works inside scope)
        bool editor_hovered = ImNodes::IsEditorHovered();

        ImNodes::EndNodeEditor();

        // Pin tooltips (after EndNodeEditor per imnodes query pattern)
        showPinTooltips();
        showNodeHoverTooltips();

        // Process interactions after EndNodeEditor (IsNodeHovered requires scope None)
        handleLinkCreation();
        handleLinkDeletion();
        handleProbeClick();
        handleContextMenu(editor_hovered);
        handleNodeDeletion();
    }
    ImGui::End();
}

void NodeGraphWidget::drawNodes() {
    for (const auto &node : m_engine.nodes()) {
        ImNodes::BeginNode(node.node_id);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(node.label.c_str());
        ImNodes::EndNodeTitleBar();

        // Icon body (pixel art from registry, or empty area if no icon loaded)
        ImTextureID tex = m_icons.get(node.label);
        if (tex) {
            ImGui::Image(tex, ImVec2(80, 56));
        } else {
            ImGui::Dummy(ImVec2(80, 56));
        }

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

        ImNodes::EndNode();
    }
}

void NodeGraphWidget::drawLinks() {
    for (const auto &link : m_engine.links()) {
        ImNodes::Link(link.link_id, link.start_pin_id, link.end_pin_id);
    }
}

void NodeGraphWidget::handleContextMenu(bool editor_hovered) {
    bool right_click = ImGui::IsMouseReleased(ImGuiMouseButton_Right);

    if (right_click && editor_hovered) {
        int hovered_node = -1;
        bool node_hovered = ImNodes::IsNodeHovered(&hovered_node);

        if (node_hovered) {
            ImGui::OpenPopup("node_context_menu");
            m_context_menu_node = hovered_node;
        } else {
            ImGui::OpenPopup("canvas_context_menu");
        }
    }

    // Render popups unconditionally so they stay open across frames
    if (ImGui::BeginPopup("node_context_menu")) {
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
        if (ImGui::MenuItem("Add Coax Cable")) {
            if (onAddCoaxCable) onAddCoaxCable();
        }
        if (ImGui::MenuItem("Add Mixer")) {
            if (onAddMixer) onAddMixer();
        }
        if (ImGui::MenuItem("Add S-Param Component")) {
            if (onAddSParamComponent) onAddSParamComponent();
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
        ImGui::EndPopup();
    }
}

void NodeGraphWidget::handleLinkCreation() {
    int start_pin, end_pin;
    m_link_created = ImNodes::IsLinkCreated(&start_pin, &end_pin);
    if (m_link_created) {
        m_engine.addLink(start_pin, end_pin);
    }
}

void NodeGraphWidget::handleLinkDeletion() {
    int link_id;
    if (ImNodes::IsLinkDestroyed(&link_id)) {
        m_engine.removeLink(link_id);
    }
}

void NodeGraphWidget::handleNodeDeletion() {
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        int num_selected = ImNodes::NumSelectedNodes();
        if (num_selected > 0) {
            std::vector<int> selected_nodes(num_selected);
            ImNodes::GetSelectedNodes(selected_nodes.data());
            for (int node_id : selected_nodes) {
                if (onRemoveNode) onRemoveNode(node_id);
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

}

void NodeGraphWidget::rebuildSynthMaps() {
    m_synth_pin_to_real_pin.clear();
    m_phantom_id_for_node.clear();
    int phantom_counter = 200000;
    for (const auto& g : m_engine.groups()) {
        for (const auto& bp : g.boundary_pins) {
            m_synth_pin_to_real_pin[bp.id] = bp.internal_pin_id;
        }
        if (m_use_phantom_nodes) {
            for (int nid : g.member_node_ids) {
                m_phantom_id_for_node[nid] = phantom_counter++;
            }
        }
    }
}

void NodeGraphWidget::drawGroupBackgrounds() {
    // Phantoms are not used in v1 (spike confirmed). Group background is drawn here;
    // internals (when expanded) are drawn by drawNodes() afterward, on top of this background.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (const auto& g : m_engine.groups()) {
        if (g.collapsed) continue;
        if (g.member_node_ids.empty()) continue;

        // Compute bounding box in grid space
        ImVec2 top_left(std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max());
        ImVec2 bottom_right(std::numeric_limits<float>::lowest(),
                            std::numeric_limits<float>::lowest());
        const float NODE_W = 120.0f, NODE_H = 80.0f;  // approximate; refined in spike
        for (int nid : g.member_node_ids) {
            ImVec2 pos = ImNodes::GetNodeGridSpacePos(nid);
            top_left.x = std::min(top_left.x, pos.x);
            top_left.y = std::min(top_left.y, pos.y);
            bottom_right.x = std::max(bottom_right.x, pos.x + NODE_W);
            bottom_right.y = std::max(bottom_right.y, pos.y + NODE_H);
        }
        if (top_left.x > bottom_right.x) continue;  // no valid members

        // Convert grid space to screen space
        ImVec2 pad(16, 16);
        ImVec2 tl_screen = top_left - pad + ImNodes::EditorContextGetPanning();
        ImVec2 br_screen = bottom_right + pad + ImNodes::EditorContextGetPanning();

        dl->AddRectFilled(tl_screen, br_screen, IM_COL32(80, 80, 120, 24));
        dl->AddRect(tl_screen, br_screen, IM_COL32(120, 120, 180, 96));

        // Title bar at the top of the rectangle
        drawGroupTitleBar(const_cast<Group&>(g), top_left);
    }
}

void NodeGraphWidget::drawPhantomNodes() {
    // Real implementation is added in Task 14 only if the spike report
    // says the phantom workaround is needed.
}

void NodeGraphWidget::drawGroupCollapsedBlocks() {
    for (const auto& g : m_engine.groups()) {
        if (!g.collapsed) continue;

        // Compute centroid in grid space
        if (g.member_node_ids.empty()) continue;
        ImVec2 sum(0, 0);
        int count = 0;
        for (int nid : g.member_node_ids) {
            ImVec2 pos = ImNodes::GetNodeGridSpacePos(nid);
            sum.x += pos.x;
            sum.y += pos.y;
            ++count;
        }
        if (count == 0) continue;
        ImVec2 centroid(sum.x / count, sum.y / count);
        ImVec2 node_pos = centroid - ImVec2(60, 40);  // center the 120x80 block

        // Render the block as an imnodes node
        ImNodes::BeginNode(g.id);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(g.name.c_str());

        // Expand button (▶) in the title bar
        ImGui::SameLine();
        if (ImGui::SmallButton("\xE2\x96\xB6")) {  // ▶
            m_engine.setGroupCollapsed(g.id, false);
        }
        ImNodes::EndNodeTitleBar();

        // Body
        ImGui::Dummy(ImVec2(120, 60));

        // Boundary pins
        int input_idx = 0, output_idx = 0;
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
                (void)output_idx++;
            } else {
                ImNodes::BeginInputAttribute(bp.id);
                ImGui::TextUnformatted(bp.label.c_str());
                ImNodes::EndInputAttribute();
                (void)input_idx++;
            }
            if (slot >= 0) {
                ImNodes::PopColorStyle();
                ImNodes::PopColorStyle();
            }
        }

        ImNodes::EndNode();
        (void)node_pos;  // imnodes uses GetNodeGridSpacePos for layout; explicit pos is read-only
    }
}

void NodeGraphWidget::drawGroupTitleBar(Group& g, const ImVec2& top_left_grid) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 tl_screen = top_left_grid - ImVec2(16, 16) + ImNodes::EditorContextGetPanning();
    ImVec2 br_screen = top_left_grid + ImVec2(180, 8) + ImNodes::EditorContextGetPanning();

    // Background of the title bar
    dl->AddRectFilled(tl_screen, br_screen, IM_COL32(60, 60, 100, 200));

    // Group name
    dl->AddText(tl_screen + ImVec2(8, 4), IM_COL32(255, 255, 255, 255), g.name.c_str());

    // Collapse button (▼)
    ImVec2 btn_min = br_screen - ImVec2(24, 0);
    ImVec2 btn_max = br_screen;
    dl->AddRectFilled(btn_min, btn_max, IM_COL32(100, 100, 140, 255));
    dl->AddText(btn_min + ImVec2(6, 2), IM_COL32(255, 255, 255, 255), "\xE2\x96\xBC");  // ▼

    // Hit-test the button
    if (ImGui::IsMouseClicked(0)) {
        ImVec2 mouse = ImGui::GetMousePos();
        if (mouse.x >= btn_min.x && mouse.x <= btn_max.x &&
            mouse.y >= btn_min.y && mouse.y <= btn_max.y) {
            m_engine.setGroupCollapsed(g.id, true);
        }
    }
}

void NodeGraphWidget::handleRubberBand() {
    // Real implementation is added in a later task.
}

void NodeGraphWidget::handleGroupSelection() {
    // Real implementation is added in a later task.
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
