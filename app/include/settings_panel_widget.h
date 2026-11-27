#pragma once

class UiFontController;

// The Settings window (Settings > UI Font...): the monospaced font picker. Picking a family applies
// it at once through UiFontController; the window only presents the controller's state.
class SettingsPanelWidget {
  public:
    void draw(bool *p_open, UiFontController &fonts);
};
