#include "imgui.h"
#include "imnodes.h"
#include "node_graph_widget.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace {

// The editor's probe gesture, as implemented by
// NodeGraphWidget::handleProbeClick(): Ctrl+click on a pin or a node body adds a
// probe (the Spectrum Analyzer then plots that signal), Shift+click removes it.
// These hints are the discoverability surface for that chord, so they must keep
// saying exactly what that handler does.
constexpr const char *kProbeHint = "Ctrl+click: probe (show in Spectrum Analyzer)";
constexpr const char *kUnprobeHint = "Shift+click: remove probe";

// Tooltips belong to an idle mouse: a node/link drag or a middle-drag canvas pan
// would otherwise drag a tooltip around with the cursor, and an open context menu
// should not keep the node it was opened on described underneath it.
bool hoverTooltipIdle() {
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return false;
    return !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
           !ImGui::IsMouseDown(ImGuiMouseButton_Middle);
}

// Body of a signal tooltip, without Begin/End so callers can append their own
// interaction hints. `spec` is null for an input pin with no upstream link.
void drawSignalBody(const Spectrum *spec, const char *direction) {
    if (!spec) {
        ImGui::Text("%s: not connected", direction);
        return;
    }

    if (spec->frequencies.empty() && spec->tones.empty()) {
        ImGui::Text("%s: no signal", direction);
        return;
    }

    int num_tones = static_cast<int>(spec->tones.size());
    if (num_tones > 0) {
        double strongest_power = -std::numeric_limits<double>::infinity();
        double strongest_freq = 0.0;
        for (const auto &t : spec->tones) {
            if (t.power_dBm > strongest_power) {
                strongest_power = t.power_dBm;
                strongest_freq = t.freq_Hz;
            }
        }
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Tones: %d  |  Strongest: %.3f MHz @ %.1f dBm", num_tones,
                      strongest_freq / 1e6, strongest_power);
        ImGui::TextUnformatted(buf);
    } else {
        ImGui::Text("Tones: 0");
    }

    if (!spec->noise_total_W.empty()) {
        double sum = 0.0;
        for (double n : spec->noise_total_W)
            sum += n;
        double avg_W = sum / static_cast<double>(spec->noise_total_W.size());
        double avg_dBm_per_Hz = 10.0 * std::log10(avg_W) + 30.0;
        ImGui::Text("Noise floor: %.1f dBm/Hz", avg_dBm_per_Hz);
    } else {
        ImGui::Text("Noise floor: -- dBm/Hz");
    }

    if (!spec->frequencies.empty()) {
        double f_min = spec->frequencies.front();
        double f_max = spec->frequencies.back();
        double f_center = (f_min + f_max) / 2.0;

        auto fmt_freq = [](double hz) -> std::string {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.3f MHz", hz / 1e6);
            return buf;
        };
        ImGui::Text("Freq range: %s - %s (center: %s)", fmt_freq(f_min).c_str(),
                    fmt_freq(f_max).c_str(), fmt_freq(f_center).c_str());
    }
}

// A pin tooltip: what the signal is, then what the user can do with this pin.
// `probe_slot` is the 0-based probe colour slot, or -1 when not probed.
void showSignalTooltip(const Spectrum *spec, const char *direction, bool is_output,
                       int probe_slot) {
    ImGui::BeginTooltip();
    drawSignalBody(spec, direction);
    ImGui::Separator();
    if (probe_slot >= 0) {
        ImGui::Text("Probed [%d]", probe_slot + 1);
        ImGui::TextDisabled("%s", kUnprobeHint);
    } else if (is_output || spec != nullptr) {
        ImGui::TextDisabled("%s", kProbeHint);
    } else {
        ImGui::TextDisabled("Drag from an output pin to connect");
    }
    ImGui::EndTooltip();
}

// Label of the node owning `pin_id` (empty when the pin resolves to no node).
std::string nodeLabelForPin(const NodeGraphEngine &engine, int pin_id) {
    const int node_id = engine.nodeIdForPin(pin_id);
    if (node_id < 0)
        return {};
    for (const auto &node : engine.nodes()) {
        if (node.node_id == node_id)
            return node.label;
    }
    return {};
}

} // namespace

void NodeGraphWidget::showPinTooltips() {
    if (!hoverTooltipIdle())
        return;

    int hovered_pin = -1;
    if (!ImNodes::IsPinHovered(&hovered_pin))
        return;

    for (const auto &node : m_engine.nodes()) {
        const auto *signal = node.signal_node;
        if (!signal)
            continue;

        for (size_t i = 0; i < node.input_pin_ids.size(); ++i) {
            if (node.input_pin_ids[i] != hovered_pin)
                continue;
            const Spectrum *spec = (i < signal->inputs.size()) ? signal->inputs[i] : nullptr;
            showSignalTooltip(spec, "IN", /*is_output=*/false,
                              m_engine.probeSlotForPin(hovered_pin));
            return;
        }

        for (size_t i = 0; i < node.output_pin_ids.size(); ++i) {
            if (node.output_pin_ids[i] != hovered_pin)
                continue;
            const Spectrum *spec = (i < signal->outputs.size()) ? &signal->outputs[i] : nullptr;
            showSignalTooltip(spec, "OUT", /*is_output=*/true,
                              m_engine.probeSlotForPin(hovered_pin));
            return;
        }
    }

    // Boundary pin tooltips (synthesized pins with ids >= 100000)
    if (hovered_pin >= 100000) {
        auto it = m_synth_pin_to_real_pin.find(hovered_pin);
        if (it == m_synth_pin_to_real_pin.end())
            return;
        const int real_pin = it->second;
        for (const auto &node : m_engine.nodes()) {
            for (size_t i = 0; i < node.output_pin_ids.size(); ++i) {
                if (node.output_pin_ids[i] != real_pin || !node.signal_node)
                    continue;
                if (i < node.signal_node->outputs.size()) {
                    showSignalTooltip(&node.signal_node->outputs[i], "OUT", true,
                                      m_engine.probeSlotForPin(real_pin));
                    return;
                }
            }
            for (size_t i = 0; i < node.input_pin_ids.size(); ++i) {
                if (node.input_pin_ids[i] != real_pin || !node.signal_node)
                    continue;
                const Spectrum *spec =
                    (i < node.signal_node->inputs.size()) ? node.signal_node->inputs[i] : nullptr;
                showSignalTooltip(spec, "IN", false, m_engine.probeSlotForPin(real_pin));
                return;
            }
        }
    }
}

void NodeGraphWidget::showNodeHoverTooltips() {
    if (!hoverTooltipIdle())
        return;

    // A hovered pin already owns the tooltip (showPinTooltips); stacking the node
    // tooltip on top of it would put two boxes under the cursor at once.
    int hovered_pin = -1;
    if (ImNodes::IsPinHovered(&hovered_pin))
        return;

    int hovered_node = -1;
    if (!ImNodes::IsNodeHovered(&hovered_node))
        return;

    // Collapsed subcircuit block (block ids are allocated from 50000). The name
    // is project data, so it is never interpolated into a format string.
    if (const Group *group = m_engine.groupById(hovered_node)) {
        // A block Ctrl+click probes the group's first *output* boundary pin
        // (handleProbeClick), so the hint is only printed when one exists: a
        // subcircuit with no cross-boundary output link has nothing to probe.
        const bool probeable = std::any_of(group->boundary_pins.begin(), group->boundary_pins.end(),
                                           [](const GroupBoundaryPin &bp) { return bp.is_output; });
        const std::string title = "Subcircuit: " + group->name;
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(title.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (probeable)
            ImGui::TextDisabled("%s", kProbeHint);
        ImGui::TextDisabled("Right-click: expand / rename / ungroup");
        ImGui::EndTooltip();
        return;
    }

    const std::string summary = onNodeHover ? onNodeHover(hovered_node) : std::string();
    if (summary.empty())
        return;

    // Ctrl+click on a node body probes its first output, so the hint reports
    // whether that port is already probed and flags the multi-output case where
    // a specific port has to be clicked instead.
    int first_output = -1;
    size_t num_outputs = 0;
    for (const auto &node : m_engine.nodes()) {
        if (node.node_id != hovered_node)
            continue;
        num_outputs = node.output_pin_ids.size();
        if (num_outputs > 0)
            first_output = node.output_pin_ids[0];
        break;
    }
    const int probe_slot = (first_output >= 0) ? m_engine.probeSlotForPin(first_output) : -1;

    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
    ImGui::TextUnformatted(summary.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Separator();
    if (probe_slot >= 0) {
        ImGui::Text("Probed [%d]", probe_slot + 1);
        ImGui::TextDisabled("%s", kUnprobeHint);
    } else if (first_output >= 0) {
        ImGui::TextDisabled("%s", kProbeHint);
    }
    if (num_outputs > 1)
        ImGui::TextDisabled("Ctrl+click an output pin to probe that port");
    ImGui::TextDisabled("Right-click: duplicate / remove");
    ImGui::EndTooltip();
}

void NodeGraphWidget::showLinkTooltips() {
    if (!hoverTooltipIdle())
        return;

    int link_id = -1;
    if (!ImNodes::IsLinkHovered(&link_id))
        return;

    ImGui::BeginTooltip();
    for (const auto &link : m_engine.links()) {
        if (link.link_id != link_id)
            continue;
        const std::string from = nodeLabelForPin(m_engine, link.start_pin_id);
        const std::string to = nodeLabelForPin(m_engine, link.end_pin_id);
        if (!from.empty() && !to.empty()) {
            const std::string endpoints = from + " -> " + to;
            ImGui::TextUnformatted(endpoints.c_str());
        }
        break;
    }
    ImGui::Separator();
    ImGui::TextDisabled("Right-click: remove link");
    ImGui::TextDisabled("Left-click: select, then Delete");
    ImGui::EndTooltip();
}
