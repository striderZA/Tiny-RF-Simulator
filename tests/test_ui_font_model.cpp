#include <catch2/catch_test_macros.hpp>

#include "ui_font_model.h"

#include <string>
#include <vector>

using ui_font::FontCandidate;
using ui_font::FontResolution;

namespace {

FontCandidate candidate(std::string family, bool fixed_pitch, bool loadable) {
    FontCandidate c;
    c.family = std::move(family);
    c.fixed_pitch = fixed_pitch;
    c.loadable = loadable;
    return c;
}

} // namespace

TEST_CASE("Picker lists only monospaced, loadable families", "[ui_font]") {
    const auto families = ui_font::monospacedFamilies({
        candidate("Consolas", true, true),
        candidate("Arial", false, true),    // proportional: not offered
        candidate("Courier", true, false),  // bitmap or unreadable: not offered
        candidate("@Consolas", true, true), // vertical face: not offered
        candidate("", true, true),          // no name: not offered
    });
    REQUIRE(families == std::vector<std::string>{"Consolas"});
}

TEST_CASE("Picker lists each family once, keeping the first spelling", "[ui_font]") {
    const auto families = ui_font::monospacedFamilies({
        candidate("Consolas", true, true),
        candidate("CONSOLAS", true, true),
        candidate("Consolas", true, true),
        candidate("Cascadia Mono", true, true),
    });
    REQUIRE(families == std::vector<std::string>{"Cascadia Mono", "Consolas"});
}

TEST_CASE("Picker sorts families case-insensitively", "[ui_font]") {
    const auto families = ui_font::monospacedFamilies({
        candidate("courier new", true, true),
        candidate("Consolas", true, true),
        candidate("Lucida Console", true, true),
        candidate("cascadia mono", true, true),
    });
    REQUIRE(families ==
            std::vector<std::string>{"cascadia mono", "Consolas", "courier new", "Lucida Console"});
}

TEST_CASE("An empty saved choice resolves to Default", "[ui_font]") {
    const FontResolution r = ui_font::resolveSavedFamily("", {"Consolas"});
    REQUIRE(r.applied_family.empty());
    REQUIRE(r.missing_family.empty());
}

TEST_CASE("An installed saved family resolves to its installed spelling", "[ui_font]") {
    const FontResolution r = ui_font::resolveSavedFamily("consolas", {"Consolas", "Courier New"});
    REQUIRE(r.applied_family == "Consolas");
    REQUIRE(r.missing_family.empty());
}

TEST_CASE("A saved family that is no longer installed falls back to Default and is kept",
          "[ui_font]") {
    const FontResolution r = ui_font::resolveSavedFamily("Fira Code", {"Consolas"});
    REQUIRE(r.applied_family.empty());
    REQUIRE(r.missing_family == "Fira Code");
}

TEST_CASE("A saved family is never matched to a different installed family", "[ui_font]") {
    // A name that only shares a prefix with an installed family is still missing.
    const FontResolution r = ui_font::resolveSavedFamily("Consol", {"Consolas"});
    REQUIRE(r.applied_family.empty());
    REQUIRE(r.missing_family == "Consol");
}

TEST_CASE("Only single-face TrueType and OpenType files are loadable by the atlas", "[ui_font]") {
    REQUIRE(ui_font::isSingleFaceFontFile({0x00, 0x01, 0x00, 0x00, 0x00}));
    REQUIRE(ui_font::isSingleFaceFontFile({'t', 'r', 'u', 'e', 0x00}));
    REQUIRE(ui_font::isSingleFaceFontFile({'O', 'T', 'T', 'O', 0x00}));
    REQUIRE_FALSE(ui_font::isSingleFaceFontFile({'t', 't', 'c', 'f', 0x00}));
    REQUIRE_FALSE(ui_font::isSingleFaceFontFile({0x00, 0x00, 0x00}));
    REQUIRE_FALSE(ui_font::isSingleFaceFontFile({}));
}

TEST_CASE("The system enumeration yields families the reader can load", "[ui_font]") {
    // Platform facts: whatever the host has installed, every listed family must be a
    // single-face font the reader returns, and an unknown name must not be substituted.
    const auto families = ui_font::monospacedFamilies(ui_font::enumerateSystemFonts());
    for (const std::string &family : families) {
        std::vector<unsigned char> bytes;
        INFO("family: " << family);
        REQUIRE(ui_font::readSystemFontFile(family, bytes));
        REQUIRE(ui_font::isSingleFaceFontFile(bytes));
    }
    std::vector<unsigned char> bytes;
    REQUIRE_FALSE(ui_font::readSystemFontFile("No Such Font Family 4f9c2a", bytes));
    REQUIRE(bytes.empty());
}
