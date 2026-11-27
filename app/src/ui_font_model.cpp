#include "ui_font_model.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <set>
#include <string_view>

namespace ui_font {
namespace {

// ASCII case folding for family-name comparison. Windows and fontconfig family names are
// compared without regard to case, so "Consolas" and "CONSOLAS" are one family.
std::string foldCase(std::string_view text) {
    std::string folded(text);
    for (char &c : folded)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return folded;
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
