#pragma once

class TutorialState;

// Renders the guided walkthrough: a bright outline around the current step's
// target panel plus a floating "Tutorial Guide" window with the instruction
// text and navigation buttons. Requires a live ImGuiContext.
class TutorialWidget {
  public:
    // No-op when the tutorial isn't active. Button presses mutate `state`, so
    // callers should re-read state.isActive() after drawing.
    void draw(TutorialState &state);
};
