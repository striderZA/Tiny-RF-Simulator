#pragma once

#include "ui_font_model.h"

#include <string>
#include <vector>

class SessionState;
struct ImFont;

// Owns the UI font choice: the base family, its style, the emphasis face, and the size. It keeps
// the saved choice (what the user picked or app.ini holds) apart from the applied one (what the
// ImGui atlas draws with), and the visible notes for every fallback. The atlas changes only in
// applyPending(), which the frame loop calls between frames, before ImGui::NewFrame.
class UiFontController {
  public:
    // Persisted in app.ini under [Settings]. SessionState is a no-op off Windows.
    static constexpr const char *kSection = "Settings";
    static constexpr const char *kKey = "UiFont";           // family; empty = Default
    static constexpr const char *kStyleKey = "UiFontStyle"; // style name; empty = Regular
    static constexpr const char *kStyleWeightKey =
        "UiFontStyleWeight"; // OpenType weight of the style
    static constexpr const char *kStyleItalicKey = "UiFontStyleItalic"; // "1" when italic
    static constexpr const char *kEmphasisKey = "UiFontEmphasis";       // "Bold" or "Medium"
    static constexpr const char *kSizeKey = "UiFontSize"; // px at 100%; empty = built-in

    // A size request held by the slider rebuilds the atlas once it has not changed for this long.
    static constexpr double kSizeSettleSeconds = 0.25;

    // Reads the saved choice and queues it for the first applyPending().
    explicit UiFontController(SessionState &state);

    // Installs the queued or changed choice. Returns true when the atlas was rebuilt. A choice that
    // cannot be loaded keeps the applied font and sets note(). A startup fallback to Default keeps
    // the saved name, so the font returns when it is installed again.
    bool applyPending();

    // Picks a family; "" is Default. A family starts at Regular; Default restores the built-in
    // font, style, emphasis, and size.
    void select(const std::string &family);
    // Picks one of the saved family's faces for the body text.
    void selectStyle(const ui_font::FontFace &face);
    // Picks the emphasis face (Bold, or Medium where the family offers it).
    void selectEmphasis(ui_font::Emphasis emphasis);
    // Requests a size in px at 100%, clamped to the supported range. `active` is true while the
    // control is held; the atlas then waits for the request to settle, and rebuilds on release.
    void setSize(int px, bool active);

    // Monospaced families, enumerated once on first use (applyPending() enumerates at startup).
    const std::vector<std::string> &installed();
    bool enumerated() const { return m_enumerated; }

    // The family the UI uses now; empty is the built-in Default.
    const std::string &appliedFamily() const { return m_applied_family; }
    // The saved choice, which can name a font that is not installed.
    const std::string &savedFamily() const { return m_saved.family; }
    // The faces of the applied family (empty for Default), the Style list's source.
    const std::vector<ui_font::FontFace> &appliedFaces() const { return m_applied_faces; }
    // The faces the body text and the emphasis slot use now.
    const ui_font::FontFace &appliedBodyFace() const { return m_applied_body; }
    const ui_font::FontFace &appliedEmphasisFace() const { return m_applied_emphasis; }
    // The saved style and emphasis.
    const ui_font::StyleChoice &savedStyle() const { return m_saved.style; }
    ui_font::Emphasis savedEmphasis() const { return m_saved.emphasis; }
    // The size the picker shows: the saved size, or the built-in size.
    float requestedSize() const;
    // The size the atlas was last built for, in px at 100%, and the DPI scale it was built with.
    float appliedSize() const { return m_applied_size; }
    float appliedScale() const { return m_applied_scale; }
    // The built-in size, captured before this controller changed the style.
    float builtinSize() const { return m_builtin_size; }

    // The font drawn for body text, and the one for emphasis text (the same font when the face is
    // shared, for example a family without a Bold face).
    ImFont *regularFont() const { return m_regular; }
    ImFont *emphasisFont() const { return m_emphasis_font; }

    // Why the applied font differs from the saved choice, or why a choice was refused. Empty when
    // nothing needs explaining.
    const std::string &note() const { return m_note; }
    // Number of atlas rebuilds so far (tests check the size debounce against it).
    int rebuildCount() const { return m_rebuilds; }

    // Every app.ini key this choice owns, for test harnesses that save and restore them.
    static std::vector<const char *> persistedKeys();

  private:
    // The saved choice. size is 0 for the built-in size.
    struct Choice {
        std::string family;
        ui_font::StyleChoice style;
        ui_font::Emphasis emphasis = ui_font::Emphasis::Bold;
        int size = 0;
        bool operator==(const Choice &other) const;
    };
    // The discrete fields a user request changes; a refused request restores them.
    struct Discrete {
        std::string family;
        ui_font::StyleChoice style;
        ui_font::Emphasis emphasis = ui_font::Emphasis::Bold;
    };

    void ensureEnumerated();
    bool rebuild(bool user);
    void persist();
    void rememberDiscrete();
    void restoreDiscrete();

    SessionState &m_state;
    Choice m_saved;
    Choice m_persisted;
    Discrete m_before_request;
    std::vector<std::string> m_load_notes; // notes about the saved text, until the user changes it

    bool m_dirty = true; // a discrete change or the startup choice awaits applyPending()
    bool m_user_request = false;
    bool m_size_active = false; // the size control is held
    double m_size_changed_at = 0.0;

    bool m_enumerated = false;
    std::vector<ui_font::FontCandidate> m_candidates;
    std::vector<std::string> m_installed;

    std::string m_applied_family;
    std::vector<ui_font::FontFace> m_applied_faces;
    ui_font::FontFace m_applied_body;
    ui_font::FontFace m_applied_emphasis;
    float m_applied_size = 0.0f;
    float m_applied_scale = 0.0f;
    float m_builtin_size = 0.0f;
    ImFont *m_regular = nullptr;
    ImFont *m_emphasis_font = nullptr;

    std::string m_note;
    int m_rebuilds = 0;
};
