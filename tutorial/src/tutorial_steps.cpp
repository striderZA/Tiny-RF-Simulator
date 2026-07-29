#include "tutorial_steps.h"

namespace {

// Data-driven walkthrough content — add or reorder entries here to change the
// tutorial. Wording is the imperative, single-action form of the reference
// material in help/src/help_widget.cpp.
const std::vector<TutorialStep> steps = {
    {"Welcome",
     "This short walkthrough covers adding a component, connecting it, configuring it, and "
     "reading its output. Click Next to begin.",
     TutorialTarget::None},
    {"Add a Component",
     "Click a component in the highlighted Component Library panel to insert it into the canvas. "
     "You can also right-click empty canvas in the Node Editor to pick from a context menu.",
     TutorialTarget::ComponentLibrary},
    {"Connect Pins",
     "In the Node Editor, click an output pin and drag to an input pin, then release to create a "
     "link. To remove one, select the link and press Delete.",
     TutorialTarget::NodeEditor},
    {"Configure Parameters",
     "Left-click a node to load it into the Properties panel, then adjust a parameter such as "
     "gain or frequency. Changes take effect immediately.",
     TutorialTarget::Properties},
    {"View Results",
     "Click an output pin to probe it — the Spectrum Analyzer plots that node's live signal. Up "
     "to four probes can be active, each with its own trace color.",
     TutorialTarget::SpectrumAnalyzer},
    {"Navigate the Graph",
     "Pan the canvas by middle-click dragging. Ctrl+click or rubber-band select to pick several "
     "nodes at once, and press Delete to remove them. That's the whole workflow — click Finish.",
     TutorialTarget::NodeEditor},
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
    case TutorialTarget::None:
        break;
    }
    return nullptr;
}
