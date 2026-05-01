#include "node_graph_widget.h"
#include "imgui.h"
#include "imnodes.h"
#include <algorithm>
#include <cmath>
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
        ImNodes::BeginNodeEditor();

        drawNodes();
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

static void drawNodeSymbol(ImDrawList* dl, ImVec2 center, float size, const std::string& label) {
    auto startsWith = [&](const char* prefix) {
        return label.rfind(prefix, 0) == 0;
    };

    if (startsWith("Generator")) {
        ImU32 color = IM_COL32(60, 140, 220, 255);
        dl->AddCircle(center, size, color, 0, 2.0f);
        float r = size * 0.7f;
        int segs = 20;
        for (int i = 0; i < segs; ++i) {
            float t0 = (float)i / (float)segs;
            float t1 = (float)(i + 1) / (float)segs;
            float x0 = center.x - r + t0 * 2.0f * r;
            float y0 = center.y + size * 0.6f * std::sin(t0 * 2.0f * 3.14159f * 1.5f);
            float x1 = center.x - r + t1 * 2.0f * r;
            float y1 = center.y + size * 0.6f * std::sin(t1 * 2.0f * 3.14159f * 1.5f);
            dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), color, 2.0f);
        }
    } else if (startsWith("Amplifier")) {
        ImU32 color = IM_COL32(220, 80, 80, 255);
        dl->AddTriangleFilled(
            ImVec2(center.x + size * 0.6f, center.y),
            ImVec2(center.x - size * 0.6f, center.y - size * 0.8f),
            ImVec2(center.x - size * 0.6f, center.y + size * 0.8f),
            color);
    } else if (startsWith("Splitter")) {
        ImU32 color = IM_COL32(60, 180, 75, 255);
        float hs = size * 0.8f;
        dl->AddRect(ImVec2(center.x - hs, center.y - hs * 0.6f),
                    ImVec2(center.x + hs, center.y + hs * 0.6f), color, 0.0f, 0, 2.0f);
        float mid = center.x - hs * 0.3f;
        dl->AddLine(ImVec2(center.x - hs + 4, center.y), ImVec2(mid, center.y), color, 2.0f);
        dl->AddLine(ImVec2(mid, center.y), ImVec2(center.x + hs - 4, center.y - hs * 0.35f), color, 2.0f);
        dl->AddLine(ImVec2(mid, center.y), ImVec2(center.x + hs - 4, center.y + hs * 0.35f), color, 2.0f);
    } else if (startsWith("Mixer")) {
        ImU32 color = IM_COL32(230, 150, 40, 255);
        dl->AddCircle(center, size, color, 0, 2.0f);
        float d = size * 0.7f;
        dl->AddLine(ImVec2(center.x - d, center.y - d), ImVec2(center.x + d, center.y + d), color, 2.0f);
        dl->AddLine(ImVec2(center.x + d, center.y - d), ImVec2(center.x - d, center.y + d), color, 2.0f);
    } else if (startsWith("ADC")) {
        ImU32 color = IM_COL32(120, 50, 170, 255);
        float hs = size * 0.45f;
        float hw = size * 0.75f;
        dl->AddLine(ImVec2(center.x - hw, center.y), ImVec2(center.x - hs, center.y - hs), color, 2.0f);
        dl->AddLine(ImVec2(center.x - hs, center.y - hs), ImVec2(center.x - hs, center.y + hs), color, 2.0f);
        dl->AddLine(ImVec2(center.x - hs, center.y + hs), ImVec2(center.x - hw, center.y), color, 2.0f);
        dl->AddRect(ImVec2(center.x - hs, center.y - hs),
                    ImVec2(center.x + hw, center.y + hs), color, 0.0f, 0, 2.0f);
    } else if (startsWith("S-Param")) {
        ImU32 color = IM_COL32(220, 80, 80, 255);
        dl->AddTriangleFilled(
            ImVec2(center.x + size * 0.6f, center.y),
            ImVec2(center.x - size * 0.6f, center.y - size * 0.8f),
            ImVec2(center.x - size * 0.6f, center.y + size * 0.8f),
            color);
        dl->AddText(ImVec2(center.x - size * 0.2f, center.y - size * 0.35f),
                    IM_COL32(255, 255, 255, 255), "S");
    }
}

void NodeGraphWidget::drawNodes() {
    for (const auto &node : m_engine.nodes()) {
        ImNodes::BeginNode(node.node_id);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(node.label.c_str());
        ImNodes::EndNodeTitleBar();

        // Symbol body
        ImGui::Dummy(ImVec2(80, 56));
        {
            auto* dl = ImGui::GetWindowDrawList();
            ImVec2 rmin = ImGui::GetItemRectMin();
            ImVec2 rmax = ImGui::GetItemRectMax();
            ImVec2 sym_center((rmin.x + rmax.x) * 0.5f, (rmin.y + rmax.y) * 0.5f);
            drawNodeSymbol(dl, sym_center, 24.0f, node.label);
        }

        for (int pin : node.input_pin_ids) {
            ImNodes::BeginInputAttribute(pin);
            ImGui::Text("IN");
            ImNodes::EndInputAttribute();
        }

        static const ImU32 probe_colors[4] = {
            IM_COL32(22, 199, 154, 255),  // Teal
            IM_COL32(230, 150, 40, 255),  // Orange
            IM_COL32(120, 50, 170, 255),  // Purple
            IM_COL32(60, 140, 220, 255),  // Blue
        };

        for (int pin : node.output_pin_ids) {
            int slot = m_engine.probeSlotForPin(pin);
            if (slot >= 0) {
                ImNodes::PushColorStyle(ImNodesCol_Pin, probe_colors[slot]);
                ImNodes::PushColorStyle(ImNodesCol_PinHovered, probe_colors[slot]);
            }
            ImNodes::BeginOutputAttribute(pin);
            ImGui::Text("OUT");
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
        if (ImGui::MenuItem("Add Mixer")) {
            if (onAddMixer) onAddMixer();
        }
        if (ImGui::MenuItem("Add S-Param Amp")) {
            if (onAddSParamAmp) onAddSParamAmp();
        }
        if (ImGui::MenuItem("Add RF ADC")) {
            if (onAddAdc) onAddAdc();
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

} // anonymous namespace

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
            const Spectrum &spec = (i < signal->inputs.size())
                                       ? signal->inputs[i]
                                       : Spectrum();
            showSpectrumTooltip(spec, "IN");
            return;
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
