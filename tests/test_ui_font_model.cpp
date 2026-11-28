#include <catch2/catch_test_macros.hpp>

#include "ui_font_model.h"

#include <string>
#include <vector>

using ui_font::Emphasis;
using ui_font::FontCandidate;
using ui_font::FontFace;
using ui_font::FontResolution;
using ui_font::StyleChoice;

namespace {

FontCandidate candidate(std::string family, bool fixed_pitch, bool loadable) {
    FontCandidate c;
    c.family = std::move(family);
    c.fixed_pitch = fixed_pitch;
    c.loadable = loadable;
    return c;
}

FontCandidate face(std::string family, std::string style, int weight, bool italic) {
    FontCandidate c = candidate(std::move(family), true, true);
    c.style = std::move(style);
    c.weight = weight;
    c.italic = italic;
    return c;
}

// A family with every common face, as a Windows install lists it.
std::vector<FontCandidate> fullFamily(const std::string &name) {
    return {
        face(name, "Regular", 400, false), face(name, "Bold", 700, false),
        face(name, "Italic", 400, true),   face(name, "Bold Italic", 700, true),
        face(name, "Medium", 500, false),  face(name, "Light", 300, false),
    };
}

std::vector<std::string> styleNames(const std::vector<FontFace> &faces) {
    std::vector<std::string> names;
    for (const FontFace &f : faces)
        names.push_back(f.style);
    return names;
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

TEST_CASE("A family lists its loadable faces once each, upright first by weight", "[ui_font]") {
    std::vector<FontCandidate> candidates = fullFamily("Consolas");
    // A second charset report of Bold, and an unloadable Black face that must not be listed.
    candidates.push_back(face("CONSOLAS", "Bold", 700, false));
    FontCandidate black = face("Consolas", "Black", 900, false);
    black.loadable = false;
    candidates.push_back(black);

    const auto faces = ui_font::familyFaces(candidates, "consolas");
    REQUIRE(styleNames(faces) == std::vector<std::string>{"Light", "Regular", "Medium", "Bold",
                                                          "Italic", "Bold Italic"});
}

TEST_CASE("A face with an empty style name is listed by its weight and slant", "[ui_font]") {
    std::vector<FontCandidate> candidates = {face("Mono", "", 700, false)};
    const auto faces = ui_font::familyFaces(candidates, "Mono");
    REQUIRE(faces.size() == 1);
    REQUIRE(faces[0].weight == 700);
    REQUIRE_FALSE(faces[0].italic);
    REQUIRE_FALSE(faces[0].style.empty());
}

TEST_CASE("The body face is Regular when chosen, and emphasis is Bold by default", "[ui_font]") {
    const auto faces = ui_font::familyFaces(fullFamily("Consolas"), "Consolas");
    const ui_font::FacePlan plan =
        ui_font::planFaces("Consolas", faces, StyleChoice{}, Emphasis::Bold);
    REQUIRE(plan.has_face);
    REQUIRE(plan.body.style == "Regular");
    REQUIRE(plan.emphasis.style == "Bold");
    REQUIRE(plan.notes.empty());
}

TEST_CASE("A chosen style is the body face", "[ui_font]") {
    const auto faces = ui_font::familyFaces(fullFamily("Consolas"), "Consolas");
    StyleChoice italic;
    italic.name = "Italic";
    italic.weight = 400;
    italic.italic = true;
    const ui_font::FacePlan plan = ui_font::planFaces("Consolas", faces, italic, Emphasis::Bold);
    REQUIRE(plan.body.style == "Italic");
    REQUIRE(plan.body.italic);
    REQUIRE(plan.emphasis.style == "Bold");
    REQUIRE(plan.notes.empty());
}

TEST_CASE("Medium is offered as the emphasis face when the family has it", "[ui_font]") {
    const auto faces = ui_font::familyFaces(fullFamily("Consolas"), "Consolas");
    const ui_font::FacePlan plan =
        ui_font::planFaces("Consolas", faces, StyleChoice{}, Emphasis::Medium);
    REQUIRE(plan.emphasis.style == "Medium");
    REQUIRE(plan.notes.empty());
}

TEST_CASE("A family with no Bold face uses its Regular face and says so", "[ui_font]") {
    // Missing bold must not silently break bold text: the emphasis slot takes the Regular face.
    const auto faces = ui_font::familyFaces(
        {face("Lite Mono", "Regular", 400, false), face("Lite Mono", "Italic", 400, true)},
        "Lite Mono");
    const ui_font::FacePlan plan =
        ui_font::planFaces("Lite Mono", faces, StyleChoice{}, Emphasis::Bold);
    REQUIRE(plan.has_face);
    REQUIRE(plan.emphasis.style == "Regular");
    REQUIRE(plan.emphasis.weight == 400);
    REQUIRE_FALSE(plan.emphasis.italic);
    REQUIRE(plan.notes.size() == 1);
    REQUIRE(plan.notes[0].find("no Bold face") != std::string::npos);
}

TEST_CASE("Medium falls back to Bold, with a note, when the family has no Medium", "[ui_font]") {
    const auto faces = ui_font::familyFaces(
        {face("Mono", "Regular", 400, false), face("Mono", "Bold", 700, false)}, "Mono");
    const ui_font::FacePlan plan =
        ui_font::planFaces("Mono", faces, StyleChoice{}, Emphasis::Medium);
    REQUIRE(plan.emphasis.style == "Bold");
    REQUIRE(plan.notes.size() == 1);
    REQUIRE(plan.notes[0].find("no Medium face") != std::string::npos);
}

TEST_CASE("A saved style the family no longer has resolves to the nearest face, with a note",
          "[ui_font]") {
    // The family lost its Medium; the saved weight 500 resolves to the nearest upright face.
    const auto faces =
        ui_font::familyFaces({face("Mono", "Regular", 400, false),
                              face("Mono", "Light", 300, false), face("Mono", "Bold", 700, false)},
                             "Mono");
    StyleChoice saved;
    saved.name = "Medium";
    saved.weight = 500;
    saved.italic = false;
    const ui_font::FacePlan plan = ui_font::planFaces("Mono", faces, saved, Emphasis::Bold);
    REQUIRE(plan.body.style == "Regular");
    REQUIRE(plan.notes.size() == 1);
    REQUIRE(plan.notes[0].find("\"Medium\"") != std::string::npos);
    REQUIRE(plan.notes[0].find("\"Regular\"") != std::string::npos);
}

TEST_CASE("A missing saved style keeps its slant when the family has one of that slant",
          "[ui_font]") {
    const auto faces = ui_font::familyFaces(
        {face("Mono", "Regular", 400, false), face("Mono", "Light Italic", 300, true)}, "Mono");
    StyleChoice saved;
    saved.name = "Bold Italic";
    saved.weight = 700;
    saved.italic = true;
    const ui_font::FacePlan plan = ui_font::planFaces("Mono", faces, saved, Emphasis::Bold);
    REQUIRE(plan.body.style == "Light Italic");
}

TEST_CASE("Saved size: absent is the built-in size, a whole number in range is kept", "[ui_font]") {
    REQUIRE(ui_font::resolveSavedSize("").size_px == 0);
    REQUIRE(ui_font::resolveSavedSize("").note.empty());
    REQUIRE(ui_font::resolveSavedSize("18").size_px == 18);
    REQUIRE(ui_font::resolveSavedSize("18").note.empty());
    REQUIRE(ui_font::resolveSavedSize("10").size_px == 10);
    REQUIRE(ui_font::resolveSavedSize("24").size_px == 24);
}

TEST_CASE("Saved size outside the range is clamped to the nearest limit, with a note",
          "[ui_font]") {
    const ui_font::SizeResolution high = ui_font::resolveSavedSize("40");
    REQUIRE(high.size_px == ui_font::kMaxSizePx);
    REQUIRE_FALSE(high.note.empty());
    const ui_font::SizeResolution low = ui_font::resolveSavedSize("4");
    REQUIRE(low.size_px == ui_font::kMinSizePx);
    REQUIRE_FALSE(low.note.empty());
}

TEST_CASE("Saved size that is not a number falls back to the built-in size, with a note",
          "[ui_font]") {
    const ui_font::SizeResolution r = ui_font::resolveSavedSize("big");
    REQUIRE(r.size_px == 0);
    REQUIRE_FALSE(r.note.empty());
}

TEST_CASE("Saved emphasis: absent is Bold, Medium is kept, anything else is Bold with a note",
          "[ui_font]") {
    REQUIRE(ui_font::resolveSavedEmphasis("").emphasis == Emphasis::Bold);
    REQUIRE(ui_font::resolveSavedEmphasis("").note.empty());
    REQUIRE(ui_font::resolveSavedEmphasis("medium").emphasis == Emphasis::Medium);
    REQUIRE(ui_font::resolveSavedEmphasis("medium").note.empty());
    const ui_font::EmphasisResolution odd = ui_font::resolveSavedEmphasis("Black");
    REQUIRE(odd.emphasis == Emphasis::Bold);
    REQUIRE_FALSE(odd.note.empty());
    REQUIRE(ui_font::emphasisName(Emphasis::Medium) == "Medium");
}

TEST_CASE("The system enumeration yields faces the reader can load", "[ui_font]") {
    // Platform facts: whatever the host has installed, every listed face must be a single-face
    // font the reader returns, and an unknown name must not be substituted.
    const auto candidates = ui_font::enumerateSystemFonts();
    for (const std::string &family : ui_font::monospacedFamilies(candidates)) {
        for (const FontFace &face : ui_font::familyFaces(candidates, family)) {
            std::vector<unsigned char> bytes;
            INFO("family: " << family << " face: " << face.style);
            REQUIRE(ui_font::readSystemFontFace(family, face, bytes));
            REQUIRE(ui_font::isSingleFaceFontFile(bytes));
        }
    }
    FontFace regular;
    regular.style = "Regular";
    std::vector<unsigned char> bytes;
    REQUIRE_FALSE(ui_font::readSystemFontFace("No Such Font Family 4f9c2a", regular, bytes));
    REQUIRE(bytes.empty());
}
