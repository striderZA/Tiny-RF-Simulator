#pragma once

#include <vector>

// Panel a tutorial step points at. Each value maps to the exact title string
// the app passes to that panel's draw()/ImGui::Begin() call.
enum class TutorialTarget {
    None, // no panel highlight (welcome / wrap-up steps)
    ComponentLibrary,
    NodeEditor,
    Properties,
    SpectrumAnalyzer,
};

struct TutorialStep {
    const char *title;
    const char *instruction;
    TutorialTarget target;
};

// Ordered walkthrough content. Add, reorder, or remove entries in
// tutorial_steps.cpp to change the tutorial — nothing else hard-codes a count.
const std::vector<TutorialStep> &tutorialSteps();

// ImGui window title for a target, or nullptr for TutorialTarget::None.
const char *tutorialTargetWindowTitle(TutorialTarget target);
