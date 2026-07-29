#pragma once

#include "tutorial_steps.h"
#include <string>

// Navigation state for the guided walkthrough, plus the durable "user has
// already been offered the tutorial" flag. Pure logic and std::filesystem only —
// no ImGuiContext required, so it is unit-testable under tests/.
//
// Completion is persisted as an exe-relative marker file rather than through
// SessionState, which is a no-op outside Windows.
class TutorialState {
  public:
    TutorialState();

    // <exe_dir>/.tutorial_completed
    std::string markerPath() const;
    bool completed() const;
    // Idempotent; creates the marker file if it doesn't already exist.
    void markCompleted();

    // Rewinds to the first step and activates the walkthrough.
    void start();
    // Advances one step. On the last step this finishes the tutorial:
    // marks it completed and deactivates.
    void next();
    // No-op on the first step.
    void back();
    // Jumps to the last step without finishing.
    void skipToLast();
    // Deactivates without marking completed.
    void exit();

    bool isActive() const { return m_active; }
    bool atFirstStep() const { return m_step_index == 0; }
    bool atLastStep() const;
    int stepIndex() const { return m_step_index; }
    int stepCount() const { return static_cast<int>(tutorialSteps().size()); }
    const TutorialStep &currentStep() const;

  private:
    std::string m_exe_dir;
    bool m_active = false;
    int m_step_index = 0;
};
