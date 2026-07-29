#include "help_widget.h"
#include <imgui.h>
#include <vector>

namespace {

struct HelpSection {
    const char *title;
    const char *bullets[8]; // max 8 bullets per section
    int bullet_count;
};

// Data-driven help content — add new sections here to extend the help window.
const HelpSection sections[] = {
    {"Navigating the Node Graph",
     {
         "Pan: middle-click drag",
         "Zoom: mouse wheel",
         "Select a node: left-click on it",
         "Select multiple nodes: Ctrl+click or rubber-band selection",
         "Delete selected nodes: press Delete key",
         "Right-click canvas: open context menu to add components",
         "New here? Help > Tutorial runs a guided walkthrough of this workflow",
     },
     7},
    {"Adding and Removing Components",
     {
         "Right-click the canvas and choose a component from the context menu",
         "Or click a component in the Component Library panel to insert it",
         "Connect pins: click an output pin, drag to an input pin, release",
         "Disconnect: select the link and press Delete, or right-click and remove",
         "Remove a node: select it and press Delete",
     },
     5},
    {"Configuring Parameters",
     {
         "Select a node to load its properties in the Inspector panel",
         "Use sliders, text fields, and dropdowns to adjust parameters",
         "Changes take effect immediately in the signal chain",
         "Use the Component Library to browse available parts with data files",
     },
     4},
    {"Viewing Signal Chain Results",
     {
         "Click an output pin to probe it — the Spectrum Analyzer shows that node's signal",
         "Up to 4 probes supported, each with a distinct color (teal, orange, purple, blue)",
         "Toggle individual probe trace visibility in the Spectrum Analyzer legend",
         "PFB Channelizer nodes produce IQ plots showing per-channel output",
         "Each widget can be resized, docked, or hidden via the View menu",
         "Window visibility is saved and restored between sessions",
     },
     6},
    {"Using the Inspector Panel",
     {
         "The Inspector (Properties) panel shows details for the selected node",
         "Parameters include gain, frequency, attenuation, filter type, etc.",
         "Some components support Touchstone (.s2p/.s4p) data file loading",
         "The panel updates in real time as you change values",
     },
     4},
};

} // namespace

void HelpWidget::draw(const char *title, bool *p_open) {
    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Tiny RF Simulator — Quick Reference");
    ImGui::Separator();
    ImGui::Spacing();

    for (const auto &section : sections) {
        if (ImGui::CollapsingHeader(section.title, ImGuiTreeNodeFlags_DefaultOpen)) {
            for (int i = 0; i < section.bullet_count; ++i) {
                ImGui::BulletText("%s", section.bullets[i]);
            }
            ImGui::Spacing();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Press F1 or use Help > How to Use to toggle this window.");

    ImGui::End();
}
