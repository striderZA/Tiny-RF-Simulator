#include "ui_font_controller.h"

#include "imgui.h"
#include "logging_core.h"
#include "session_state.h"
#include "ui_font_model.h"

#include <cstdio>
#include <cstring>
#include <utility>

namespace {

std::string quotedName(const std::string &family) { return "\"" + family + "\""; }

} // namespace

UiFontController::UiFontController(SessionState &state)
    : m_state(state), m_saved(state.load(kSection, kKey, "")), m_request(m_saved) {}

void UiFontController::ensureEnumerated() {
    if (m_enumerated)
        return;
    m_enumerated = true;
    m_installed = ui_font::monospacedFamilies(ui_font::enumerateSystemFonts());
    LOG_INFO("Settings: %zu monospaced UI font families found", m_installed.size());
}

const std::vector<std::string> &UiFontController::installed() {
    ensureEnumerated();
    return m_installed;
}

void UiFontController::select(const std::string &family) {
    m_request = family;
    m_has_request = true;
    m_user_request = true;
}

bool UiFontController::applyPending() {
    if (!m_has_request)
        return false;
    m_has_request = false;
    ensureEnumerated();

    if (m_user_request) {
        m_user_request = false;
        if (m_request == m_applied) {
            // Re-picking the font in use: nothing to rebuild, but a choice made over a missing
            // saved name still replaces it.
            if (m_request != m_saved)
                m_state.save(kSection, kKey, m_request.c_str());
            m_saved = m_request;
            m_note.clear();
            return false;
        }
        const InstallResult result = installFamily(m_request);
        if (result == InstallResult::Installed) {
            m_saved = m_request;
            m_state.save(kSection, kKey, m_saved.c_str());
            m_note.clear();
            return true;
        }
        LOG_WARN("Settings: UI font %s could not be loaded", quotedName(m_request).c_str());
        m_note = quotedName(m_request) + " could not be loaded. " +
                 (m_applied.empty() ? std::string("Showing Default.")
                                    : quotedName(m_applied) + " is kept.");
        return result == InstallResult::FellBackToDefault;
    }

    // Startup: the saved choice, resolved against what the system has installed now.
    const ui_font::FontResolution resolution = ui_font::resolveSavedFamily(m_request, m_installed);
    if (!resolution.missing_family.empty()) {
        LOG_WARN("Settings: saved UI font %s is not installed; using Default",
                 quotedName(resolution.missing_family).c_str());
        m_note = "Saved font " + quotedName(resolution.missing_family) +
                 " is not installed. Showing Default; the choice is kept and returns when the font "
                 "is installed.";
        return false;
    }
    if (resolution.applied_family.empty())
        return false;
    const InstallResult result = installFamily(resolution.applied_family);
    if (result == InstallResult::Installed) {
        m_note.clear();
        return true;
    }
    LOG_WARN("Settings: saved UI font %s could not be loaded; using Default",
             quotedName(resolution.applied_family).c_str());
    m_note = "Saved font " + quotedName(resolution.applied_family) +
             " could not be loaded. Showing Default; the choice is kept.";
    return result == InstallResult::FellBackToDefault;
}

UiFontController::InstallResult UiFontController::installFamily(const std::string &family) {
    std::vector<unsigned char> bytes;
    if (!family.empty() && !ui_font::readSystemFontFile(family, bytes))
        return InstallResult::Unchanged;

    ImGuiIO &io = ImGui::GetIO();
    // Between frames only (see the class comment). ClearFonts() frees the previous font's input
    // and its atlas glyphs; the backend releases the old texture after its last use.
    io.Fonts->ClearFonts();

    if (family.empty()) {
        io.FontDefault = io.Fonts->AddFontDefault();
        m_applied.clear();
        return InstallResult::Installed;
    }

    // The atlas owns a copy, allocated with ImGui's allocator, so ClearFonts() frees it with the
    // font. A non-owned buffer must outlive the font until RemoveFont() or shutdown
    // (docs/FONTS.md), and ClearFonts() on the next pick would have freed it too early.
    void *data = ImGui::MemAlloc(bytes.size());
    std::memcpy(data, bytes.data(), bytes.size());
    // The size is the style's base size, the same value the built-in font uses, so any future
    // scaling of FontSizeBase or the font scale applies to this font too.
    ImFontConfig config; // FontDataOwnedByAtlas stays true
    // The debug name lets a test confirm which family the UI is drawn with.
    std::snprintf(config.Name, sizeof(config.Name), "%s", family.c_str());
    ImFont *font = io.Fonts->AddFontFromMemoryTTF(data, static_cast<int>(bytes.size()),
                                                  ImGui::GetStyle().FontSizeBase, &config);
    if (font == nullptr) {
        io.FontDefault = io.Fonts->AddFontDefault();
        m_applied.clear();
        return InstallResult::FellBackToDefault;
    }
    io.FontDefault = font;
    m_applied = family;
    return InstallResult::Installed;
}
