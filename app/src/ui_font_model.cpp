#include "ui_font_model.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <set>
#include <string_view>
#include <system_error>
#include <tuple>

namespace ui_font {
namespace {

// Weight windows for the named faces: Bold is the upright face nearest 700, Medium the upright face
// nearest 500. A face outside its window does not count as that face.
constexpr int kRegularWeight = 400;
constexpr int kBoldWeight = 700;
constexpr int kBoldWindow = 100;
constexpr int kMediumWeight = 500;
constexpr int kMediumWindow = 50;
constexpr int kAnyWeightWindow = 1000;

// ASCII case folding for family-name comparison. Windows and fontconfig family names are
// compared without regard to case, so "Consolas" and "CONSOLAS" are one family.
std::string foldCase(std::string_view text) {
    std::string folded(text);
    for (char &c : folded)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return folded;
}

std::string quoted(const std::string &text) { return "\"" + text + "\""; }

// The name a face gets when the platform reports none. Weight bands follow the OpenType classes.
std::string styleNameFor(int weight, bool italic) {
    std::string name = weight <= 200   ? "Thin"
                       : weight <= 300 ? "Light"
                       : weight <= 450 ? "Regular"
                       : weight <= 550 ? "Medium"
                       : weight <= 650 ? "SemiBold"
                                       : "Bold";
    if (italic)
        name = name == "Regular" ? "Italic" : name + " Italic";
    return name;
}

// The face of `faces` with the given slant whose weight is nearest `target`, within `window`.
// Ties go to the lighter face. Returns false when no face of that slant is within the window.
bool nearestFace(const std::vector<FontFace> &faces, bool italic, int target, int window,
                 FontFace &out) {
    bool found = false;
    int best = 0;
    for (const FontFace &face : faces) {
        if (face.italic != italic)
            continue;
        const int distance = std::abs(face.weight - target);
        if (distance > window)
            continue;
        if (!found || distance < best || (distance == best && face.weight < out.weight)) {
            out = face;
            best = distance;
            found = true;
        }
    }
    return found;
}

// The Regular face: upright nearest 400, else the nearest face of any slant.
FontFace regularFace(const std::vector<FontFace> &faces) {
    FontFace regular;
    if (nearestFace(faces, false, kRegularWeight, kAnyWeightWindow, regular))
        return regular;
    nearestFace(faces, true, kRegularWeight, kAnyWeightWindow, regular);
    return regular;
}

// The face a saved style resolves to when no face has its name: the same slant when the family has
// one, the nearest weight first.
FontFace nearestStyle(const std::vector<FontFace> &faces, const StyleChoice &style) {
    FontFace nearest;
    if (nearestFace(faces, style.italic, style.weight, kAnyWeightWindow, nearest))
        return nearest;
    nearestFace(faces, !style.italic, style.weight, kAnyWeightWindow, nearest);
    return nearest;
}

bool sameSlantAndWeight(const FontFace &a, const FontFace &b) {
    return a.weight == b.weight && a.italic == b.italic;
}

std::string styleNameOf(const FontCandidate &candidate) {
    return candidate.style.empty() ? styleNameFor(candidate.weight, candidate.italic)
                                   : candidate.style;
}

} // namespace

std::vector<std::string> monospacedFamilies(const std::vector<FontCandidate> &candidates) {
    std::set<std::string> seen;
    std::vector<std::string> families;
    for (const FontCandidate &candidate : candidates) {
        if (!candidate.fixed_pitch || !candidate.loadable || candidate.family.empty() ||
            candidate.family.front() == '@')
            continue;
        if (!seen.insert(foldCase(candidate.family)).second)
            continue;
        families.push_back(candidate.family);
    }
    std::sort(families.begin(), families.end(), [](const std::string &a, const std::string &b) {
        const std::string folded_a = foldCase(a);
        const std::string folded_b = foldCase(b);
        if (folded_a != folded_b)
            return folded_a < folded_b;
        return a < b;
    });
    return families;
}

std::vector<FontFace> familyFaces(const std::vector<FontCandidate> &candidates,
                                  const std::string &family) {
    const std::string key = foldCase(family);
    std::vector<const FontCandidate *> matches;
    for (const FontCandidate &candidate : candidates) {
        if (!candidate.fixed_pitch || !candidate.loadable || candidate.family.empty() ||
            candidate.family.front() == '@' || foldCase(candidate.family) != key)
            continue;
        matches.push_back(&candidate);
    }
    // Upright before italic, lighter before heavier. Regular is preferred among same-weight names,
    // so it is the one that survives the dedupe below.
    std::sort(matches.begin(), matches.end(), [](const FontCandidate *a, const FontCandidate *b) {
        const std::string name_a = styleNameOf(*a);
        const std::string name_b = styleNameOf(*b);
        return std::make_tuple(a->italic, a->weight, name_a != "Regular", name_a) <
               std::make_tuple(b->italic, b->weight, name_b != "Regular", name_b);
    });
    std::vector<FontFace> faces;
    for (const FontCandidate *candidate : matches) {
        FontFace face;
        face.style = styleNameOf(*candidate);
        face.weight = candidate->weight;
        face.italic = candidate->italic;
        if (!faces.empty() && sameSlantAndWeight(faces.back(), face))
            continue;
        faces.push_back(face);
    }
    return faces;
}

FontResolution resolveSavedFamily(const std::string &saved,
                                  const std::vector<std::string> &installed) {
    FontResolution resolution;
    if (saved.empty())
        return resolution;
    const std::string key = foldCase(saved);
    for (const std::string &family : installed) {
        if (foldCase(family) == key) {
            resolution.applied_family = family;
            return resolution;
        }
    }
    resolution.missing_family = saved;
    return resolution;
}

bool isSingleFaceFontFile(const std::vector<unsigned char> &bytes) {
    if (bytes.size() < 4)
        return false;
    const unsigned char *head = bytes.data();
    const auto tagIs = [head](const char(&tag)[5]) {
        return head[0] == static_cast<unsigned char>(tag[0]) &&
               head[1] == static_cast<unsigned char>(tag[1]) &&
               head[2] == static_cast<unsigned char>(tag[2]) &&
               head[3] == static_cast<unsigned char>(tag[3]);
    };
    const bool truetype = head[0] == 0x00 && head[1] == 0x01 && head[2] == 0x00 && head[3] == 0x00;
    return truetype || tagIs("true") || tagIs("OTTO");
}

} // namespace ui_font
