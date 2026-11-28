#include "ui_font_model.h"

#include <fontconfig/fontconfig.h>

#include <cctype>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <tuple>
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

// The first `limit` bytes of a file, or the whole file when `limit` is 0.
bool readFile(const std::string &path, std::vector<unsigned char> &bytes, std::size_t limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::vector<unsigned char> data;
    if (limit > 0) {
        data.resize(limit);
        in.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(limit));
        data.resize(static_cast<std::size_t>(in.gcount()));
    } else {
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (!in.good() && !in.eof())
            return false;
    }
    bytes = std::move(data);
    return true;
}

// Reads the face a fontconfig pattern describes: family, style, OpenType weight, slant, and the
// monospaced spacing the picker requires. Returns false for a pattern that lacks any of them.
bool faceOf(FcPattern *font, FontCandidate &candidate) {
    FcChar8 *family = nullptr;
    if (FcPatternGetString(font, FC_FAMILY, 0, &family) != FcResultMatch)
        return false;
    int spacing = -1;
    if (FcPatternGetInteger(font, FC_SPACING, 0, &spacing) != FcResultMatch || spacing != FC_MONO)
        return false;
    candidate.family = reinterpret_cast<const char *>(family);

    FcChar8 *style = nullptr;
    candidate.style = FcPatternGetString(font, FC_STYLE, 0, &style) == FcResultMatch
                          ? reinterpret_cast<const char *>(style)
                          : std::string();

    int fc_weight = FC_WEIGHT_REGULAR;
    FcPatternGetInteger(font, FC_WEIGHT, 0, &fc_weight);
    candidate.weight = FcWeightToOpenType(fc_weight);

    int slant = FC_SLANT_ROMAN;
    FcPatternGetInteger(font, FC_SLANT, 0, &slant);
    candidate.italic = slant != FC_SLANT_ROMAN;
    candidate.fixed_pitch = true;
    return !candidate.family.empty();
}

bool fileOf(FcPattern *font, std::string &path) {
    FcChar8 *file = nullptr;
    if (FcPatternGetString(font, FC_FILE, 0, &file) != FcResultMatch)
        return false;
    path = reinterpret_cast<const char *>(file);
    return true;
}

// Opens a font file for ranged reads. The stream stays open until the last reader is dropped.
FileSource openFontFile(const std::string &path) {
    FileSource source;
    auto in = std::make_shared<std::ifstream>(path, std::ios::binary);
    if (!*in)
        return source;
    in->seekg(0, std::ios::end);
    const std::streamoff size = in->tellg();
    if (size <= 0)
        return source;
    source.open = true;
    source.size = static_cast<std::size_t>(size);
    source.read = [in](std::size_t offset, std::size_t length) {
        std::vector<unsigned char> bytes(length);
        in->clear();
        in->seekg(static_cast<std::streamoff>(offset));
        in->read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(length));
        bytes.resize(static_cast<std::size_t>(in->gcount()));
        return bytes;
    };
    return source;
}

// The face fontconfig selects for `family` at `face`'s weight and slant among monospaced faces.
// The match must keep the requested family, spacing, weight, and slant, so fontconfig's
// substitution is never loaded.
bool matchMonospacedFace(const std::string &family, const FontFace &face, std::string &path) {
    FcPattern *pattern = FcPatternCreate();
    if (pattern == nullptr)
        return false;
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8 *>(family.c_str()));
    FcPatternAddInteger(pattern, FC_SPACING, FC_MONO);
    FcPatternAddInteger(pattern, FC_WEIGHT, FcWeightFromOpenType(face.weight));
    FcPatternAddInteger(pattern, FC_SLANT, face.italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result = FcResultNoMatch;
    FcPattern *match = FcFontMatch(nullptr, pattern, &result);
    bool ok = false;
    if (match != nullptr && result == FcResultMatch) {
        FontCandidate matched;
        ok = faceOf(match, matched) && sameFamily(matched.family, family) &&
             matched.weight == face.weight && matched.italic == face.italic && fileOf(match, path);
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
    FcObjectSet *objects =
        FcObjectSetBuild(FC_FAMILY, FC_STYLE, FC_WEIGHT, FC_SLANT, FC_FILE, FC_SPACING, nullptr);
    FcFontSet *fonts = objects != nullptr ? FcFontList(nullptr, pattern, objects) : nullptr;

    std::set<std::tuple<std::string, std::string, int, bool>> seen;
    if (fonts != nullptr) {
        for (int i = 0; i < fonts->nfont; ++i) {
            FontCandidate candidate;
            if (!faceOf(fonts->fonts[i], candidate))
                continue;
            if (!seen.insert(
                         {candidate.family, candidate.style, candidate.weight, candidate.italic})
                     .second)
                continue;
            std::string path;
            if (fileOf(fonts->fonts[i], path))
                candidate.file = path;
            found.push_back(std::move(candidate));
        }
    }
    if (fonts != nullptr)
        FcFontSetDestroy(fonts);
    if (objects != nullptr)
        FcObjectSetDestroy(objects);
    FcPatternDestroy(pattern);
    identifyFiles(found, openFontFile);
    return found;
}

bool readSystemFontFace(const std::string &family, const FontFace &face,
                        std::vector<unsigned char> &bytes) {
    std::string path;
    std::vector<unsigned char> data;
    if (!matchMonospacedFace(family, face, path) || !readFile(path, data, 0) ||
        !isSingleFaceFontFile(data))
        return false;
    bytes = std::move(data);
    return true;
}

} // namespace ui_font
