#pragma once

#include <string>
#include <vector>

class SessionState;

// Owns the UI font choice: the installed monospaced families, the applied and saved family, and
// the visible note for a fallback or failure. The ImGui atlas changes only in applyPending(),
// which the frame loop calls between frames, before ImGui::NewFrame.
class UiFontController {
  public:
    // Persisted as [Settings] UiFont in app.ini (Windows; SessionState is a no-op elsewhere).
    static constexpr const char *kSection = "Settings";
    static constexpr const char *kKey = "UiFont";

    // Reads the saved choice and queues it for the first applyPending().
    explicit UiFontController(SessionState &state);

    // Installs the queued choice. Returns true when the UI font changed. A choice that cannot be
    // loaded keeps the current font and sets note(). A startup fallback to Default keeps the
    // saved name, so the font returns when it is installed again.
    bool applyPending();

    // Queues `family` (empty for Default) as a user choice. It applies, and is saved, on the next
    // applyPending().
    void select(const std::string &family);

    // Monospaced families, enumerated once on first use (applyPending() enumerates at startup).
    const std::vector<std::string> &installed();
    bool enumerated() const { return m_enumerated; }

    // The family the UI uses now; empty is the built-in Default.
    const std::string &appliedFamily() const { return m_applied; }
    // The saved choice, which can name a font that is not installed.
    const std::string &savedFamily() const { return m_saved; }
    // Why the applied font differs from the saved choice, or why a choice was refused. Empty when
    // nothing needs explaining.
    const std::string &note() const { return m_note; }

  private:
    void ensureEnumerated();
    // Installed: the family (Default for empty) is applied. Unchanged: the family could not be
    // read and the atlas was not touched. FellBackToDefault: the atlas was rebuilt with Default
    // after the family failed to create a font.
    enum class InstallResult { Unchanged, Installed, FellBackToDefault };
    InstallResult installFamily(const std::string &family);

    SessionState &m_state;
    std::string m_saved;
    std::string m_applied;
    std::string m_request;
    bool m_has_request = true;
    bool m_user_request = false;
    bool m_enumerated = false;
    std::vector<std::string> m_installed;
    std::string m_note;
};
