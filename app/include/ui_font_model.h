#pragma once

#include <string>
#include <vector>

// UI-free rules for the Settings font picker: which installed families are offered, how the
// saved choice resolves against them, and the platform reads that feed both. ImGui code lives in
// ui_font_controller.cpp; nothing here includes imgui.h.
namespace ui_font {

// One family as the operating system reports it.
struct FontCandidate {
    std::string family;
    bool fixed_pitch = false; // a monospaced (fixed-pitch) face
    bool loadable = false;    // readable as one TrueType or OpenType face
};

// Where the saved choice landed. An empty applied_family is the built-in Default font.
struct FontResolution {
    std::string applied_family;
    std::string missing_family; // the saved name when it is not installed, else empty
};

// The families the picker lists: fixed-pitch, loadable, not a vertical '@' face, one entry per
// family name (compared case-insensitively), sorted case-insensitively. Default is not included.
std::vector<std::string> monospacedFamilies(const std::vector<FontCandidate> &candidates);

// Resolves the saved choice. An empty `saved` is Default. A name in `installed` matches
// case-insensitively and resolves to the installed spelling. Any other name falls back to Default
// and is reported in missing_family; the caller keeps the saved name so it returns when the font
// is installed again.
FontResolution resolveSavedFamily(const std::string &saved,
                                  const std::vector<std::string> &installed);

// True when `bytes` begins with a single-face TrueType (0x00010000, 'true') or OpenType CFF
// ('OTTO') header. Font collections ('ttcf'), bitmap .fon/.pcf files, and anything else are not.
bool isSingleFaceFontFile(const std::vector<unsigned char> &bytes);

// Monospaced families as the operating system reports them, unfiltered beyond what the platform
// enumeration itself requires. Empty where the platform has no enumeration.
std::vector<FontCandidate> enumerateSystemFonts();

// Reads the single face the operating system uses for `family`. Returns false when the family is
// unknown, is not the face the system actually resolved (no silent substitution), or cannot be
// read as a single-face font. `bytes` is replaced only on success.
bool readSystemFontFile(const std::string &family, std::vector<unsigned char> &bytes);

} // namespace ui_font
