#include "tutorial_steps.h"

namespace {

// Data-driven walkthrough content — add or reorder entries here to change the
// tutorial. Wording is the imperative, single-action form of the reference
// material in help/src/help_widget.cpp.
const std::vector<TutorialStep> steps = {
    {"Welcome",
     "This short walkthrough covers adding a component, connecting it, configuring it, and "
     "reading its output, then the panels that check, budget, and automate a design. Click Next "
     "to begin.",
     TutorialTarget::None},
    {"Add a Component",
     "Click a component in the highlighted Component Library panel to insert it into the canvas. "
     "You can also right-click empty canvas in the Node Editor to pick from a context menu.",
     TutorialTarget::ComponentLibrary},
    {"Connect Pins",
     "In the Node Editor, click an output pin and drag to an input pin, then release to create a "
     "link. To remove one, select the link and press Delete; Ctrl+Z brings it back.",
     TutorialTarget::NodeEditor},
    {"Configure Parameters",
     "Left-click a node to load it into the Properties panel, then adjust a parameter such as "
     "gain or frequency. Changes take effect immediately.",
     TutorialTarget::Properties},
    {"View Results",
     "Ctrl+click an output pin to probe it — the Spectrum Analyzer plots that node's live signal. "
     "Up to four probes can be active, each with its own trace color.",
     TutorialTarget::SpectrumAnalyzer},
    {"Navigate the Graph",
     "Pan the canvas by middle-click dragging. Rubber-band select, or Ctrl+click, to pick several "
     "nodes at once — Ctrl+click on a node body also probes that node's first output. Press Delete "
     "to remove them.",
     TutorialTarget::NodeEditor},
    {"Undo and Redo",
     "Ctrl+Z undoes the last change and Ctrl+Shift+Z or Ctrl+Y redoes it, up to 100 steps back. "
     "The Edit menu names the change each will act on next. Undo covers adds, removals, links, "
     "parameter edits, probes, groups, layout, part swaps, requirements, and budget edits.",
     TutorialTarget::NodeEditor},
    {"Align and Snap",
     "Select two or more nodes and right-click one of them for Align and Distribute to tidy the "
     "layout. View > Snap to Grid makes dragged nodes land on the grid, and the right-click menu's "
     "Snap selection to grid rounds the whole selection onto it.",
     TutorialTarget::NodeEditor},
    {"Author Library Parts",
     "New Component... opens a form that defines a reusable part — parameters, ranges, and data "
     "files such as Touchstone models — and saves it to the project or your global library. "
     "Refresh reloads definitions from disk so edited parts appear without a restart; a "
     "user-defined part's Edit button reopens it in the same form.",
     TutorialTarget::ComponentLibrary},
    {"Swap a Placed Part",
     "Select a library component, open the Part dropdown in the Properties panel, and pick "
     "another part to replace it in place — position, links, probes, and group membership are "
     "kept, and the new part starts from its library parameters. A swap that would break a port "
     "is refused with the reason shown.",
     TutorialTarget::Properties},
    {"Set Receiver Requirements",
     "Open View > Receiver Requirements if it is not already on screen. It checks the circuit "
     "against named modes — each has its own band, gain, noise-figure, output-power, and IIP3 "
     "limits, plus its own input level, and Add mode defines a new one. Out-of-band suppression "
     "points set how far below the in-band gain the cascade must fall outside the band, and an "
     "SNR minimum with its bandwidth judges the output signal-to-noise.",
     TutorialTarget::ReceiverRequirements},
    {"Budget a Receiver",
     "Open View > RF Budget if it is not already on screen. The stage-table editor of the "
     "requirements-driven budget appears: Add Stage builds the chain — amplifier, attenuator, "
     "filter, coax, equalizer, splitter, or RF switch — each row takes a library part or edited "
     "settings, and Add Output picks where the cascade ends. Budget this mode gives the selected "
     "mode a budget record so it is predicted even with no overrides; the table shows each "
     "stage's gain, noise figure, and cumulative Friis and IIP3 with a verdict per output.",
     TutorialTarget::RFBudget},
    {"Automate with Test Flow",
     "Open View > Test Flow if it is not already on screen. The panel authors and runs parameter "
     "sweeps against the live circuit: New from Circuit scaffolds a flow you can edit — pick a "
     "component and one of its real parameter keys, seed the values from the circuit, then add "
     "conditions and measurements. Run applies the sweep row by row, Export... saves the results "
     "as JSON, and Save Flow... writes the flow for a later Reload.",
     TutorialTarget::TestFlow},
    {"Drive it from an Agent",
     "Open View > Agent if it is not already on screen, then enable Agent Server and point a "
     "client such as Claude Code at the rf-sim-mcp bridge that ships beside the simulator. An "
     "agent can read and edit the circuit, run measurements, sweeps, and test flows, and author "
     "library parts. Calls that change circuit, requirements, or budget state leave a checkpoint "
     "you can revert to after confirmation; read-only calls and library creation do not.",
     TutorialTarget::Agent},
    {"Wrap Up",
     "That's the tour. The panels stay under the View menu, Help > How to Use keeps the full "
     "reference, and Help > Tutorial restarts this walkthrough any time. Click Finish to mark it "
     "complete.",
     TutorialTarget::None},
};

} // namespace

const std::vector<TutorialStep> &tutorialSteps() { return steps; }

const char *tutorialTargetWindowTitle(TutorialTarget target) {
    // These strings must match the titles app.cpp passes to each panel's draw()
    // call — a mismatch silently drops the highlight instead of failing loudly.
    switch (target) {
    case TutorialTarget::ComponentLibrary:
        return "Component Library";
    case TutorialTarget::NodeEditor:
        return "Node Editor";
    case TutorialTarget::Properties:
        return "Properties";
    case TutorialTarget::SpectrumAnalyzer:
        return "Spectrum Analyzer";
    case TutorialTarget::RFBudget:
        return "RF Budget";
    case TutorialTarget::ReceiverRequirements:
        return "Receiver Requirements";
    case TutorialTarget::TestFlow:
        return "Test Flow";
    case TutorialTarget::Agent:
        return "Agent";
    case TutorialTarget::None:
        break;
    }
    return nullptr;
}
