#include "ui_font_model.h"

#include <fontconfig/fontconfig.h>

#include <cctype>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ui_font {
namespace {

bool sameFamily(const std::string &a, const std::string &b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

bool readFile(const std::string &path, std::vector<unsigned char> &bytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    if (!in.good() && !in.eof())
        return false;
    bytes = std::move(data);
    return true;
}

// The file fontconfig selects for `family` among monospaced faces. The match must keep the
// requested family and monospaced spacing, so fontconfig's substitution is never loaded.
bool matchMonospacedFile(const std::string &family, std::string &path) {
    FcPattern *pattern = FcPatternCreate();
    if (pattern == nullptr)
        return false;
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8 *>(family.c_str()));
    FcPatternAddInteger(pattern, FC_SPACING, FC_MONO);
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result = FcResultNoMatch;
    FcPattern *match = FcFontMatch(nullptr, pattern, &result);
    bool ok = false;
    if (match != nullptr && result == FcResultMatch) {
        FcChar8 *matched_family = nullptr;
        FcChar8 *file = nullptr;
        int spacing = -1;
        if (FcPatternGetString(match, FC_FAMILY, 0, &matched_family) == FcResultMatch &&
            sameFamily(reinterpret_cast<const char *>(matched_family), family) &&
            FcPatternGetInteger(match, FC_SPACING, 0, &spacing) == FcResultMatch &&
            spacing == FC_MONO && FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch) {
            path = reinterpret_cast<const char *>(file);
            ok = true;
        }
    }
    if (match != nullptr)
        FcPatternDestroy(match);
    FcPatternDestroy(pattern);
    return ok;
}

} // namespace

std::vector<FontCandidate> enumerateSystemFonts() {
    std::vector<FontCandidate> found;
    FcPattern *pattern = FcPatternCreate();
    if (pattern == nullptr)
        return found;
    FcPatternAddInteger(pattern, FC_SPACING, FC_MONO);
    FcObjectSet *objects = FcObjectSetBuild(FC_FAMILY, FC_FILE, FC_SPACING, nullptr);
    FcFontSet *fonts = objects != nullptr ? FcFontList(nullptr, pattern, objects) : nullptr;

    std::set<std::string> seen;
    if (fonts != nullptr) {
        for (int i = 0; i < fonts->nfont; ++i) {
            FcPattern *font = fonts->fonts[i];
            int spacing = -1;
            FcChar8 *family = nullptr;
            if (FcPatternGetInteger(font, FC_SPACING, 0, &spacing) != FcResultMatch ||
                spacing != FC_MONO ||
                FcPatternGetString(font, FC_FAMILY, 0, &family) != FcResultMatch)
                continue;
            FontCandidate candidate;
            candidate.family = reinterpret_cast<const char *>(family);
            if (!seen.insert(candidate.family).second)
                continue;
            candidate.fixed_pitch = true;
            std::string path;
            std::vector<unsigned char> bytes;
            candidate.loadable = matchMonospacedFile(candidate.family, path) &&
                                 readFile(path, bytes) && isSingleFaceFontFile(bytes);
            found.push_back(std::move(candidate));
        }
    }
    if (fonts != nullptr)
        FcFontSetDestroy(fonts);
    if (objects != nullptr)
        FcObjectSetDestroy(objects);
    FcPatternDestroy(pattern);
    return found;
}

bool readSystemFontFile(const std::string &family, std::vector<unsigned char> &bytes) {
    std::string path;
    std::vector<unsigned char> data;
    if (!matchMonospacedFile(family, path) || !readFile(path, data) || !isSingleFaceFontFile(data))
        return false;
    bytes = std::move(data);
    return true;
}

} // namespace ui_font
