#include "ui_font_model.h"

#include <windows.h>

#include <cstring>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <wchar.h>

namespace ui_font {
namespace {

std::wstring toWide(const std::string &utf8) {
    if (utf8.empty())
        return {};
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (length <= 0)
        return {};
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(),
                        length);
    return wide;
}

std::string toUtf8(const wchar_t *wide) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1)
        return {};
    std::string utf8(static_cast<std::size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), length, nullptr, nullptr);
    return utf8;
}

// Reads the face GDI has selected in `dc`: the whole file when `bytes` is set, else only the first
// four bytes, which is enough to tell a single-face font from a collection or a bitmap.
bool readSelectedFace(HDC dc, std::vector<unsigned char> *bytes) {
    if (bytes == nullptr) {
        unsigned char head[4] = {};
        if (GetFontData(dc, 0, 0, head, 4) != 4)
            return false;
        return isSingleFaceFontFile(std::vector<unsigned char>(head, head + 4));
    }
    const DWORD size = GetFontData(dc, 0, 0, nullptr, 0);
    if (size == GDI_ERROR || size == 0)
        return false;
    std::vector<unsigned char> data(size);
    if (GetFontData(dc, 0, 0, data.data(), size) != size || !isSingleFaceFontFile(data))
        return false;
    *bytes = std::move(data);
    return true;
}

// Reads the file of the face GDI resolves for `family` at `weight` and slant. GDI substitutes
// another face for a name or style it does not have, so the face it reports must match the request
// (family name, weight, and slant) or the read is refused.
bool readFace(const std::string &family, int weight, bool italic,
              std::vector<unsigned char> *bytes) {
    const std::wstring wide = toWide(family);
    if (wide.empty() || wide.size() >= LF_FACESIZE)
        return false;

    HDC dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr)
        return false;

    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfWeight = weight;
    lf.lfItalic = italic ? TRUE : FALSE;
    std::wcscpy(lf.lfFaceName, wide.c_str());
    HFONT font = CreateFontIndirectW(&lf);
    bool ok = false;
    if (font != nullptr) {
        HGDIOBJ previous = SelectObject(dc, font);
        wchar_t face[LF_FACESIZE] = {};
        TEXTMETRICW metrics{};
        if (GetTextFaceW(dc, LF_FACESIZE, face) > 0 && _wcsicmp(face, wide.c_str()) == 0 &&
            GetTextMetricsW(dc, &metrics) && metrics.tmWeight == weight &&
            (metrics.tmItalic != 0) == italic) {
            ok = readSelectedFace(dc, bytes);
        }
        SelectObject(dc, previous);
        DeleteObject(font);
    }
    DeleteDC(dc);
    return ok;
}

// Collects the monospaced family names. EnumFontFamiliesEx with an empty face name reports one
// face per family, so the faces are listed by a second pass per name (collectFace).
int CALLBACK collectFamily(const LOGFONTW *logfont, const TEXTMETRICW *metrics, DWORD,
                           LPARAM context) {
    auto *names = reinterpret_cast<std::set<std::wstring> *>(context);
    // TMPF_FIXED_PITCH is set for variable-pitch faces (TEXTMETRIC documentation), so a
    // fixed-pitch face has the bit clear.
    if ((metrics->tmPitchAndFamily & TMPF_FIXED_PITCH) == 0 && logfont->lfFaceName[0] != L'@')
        names->insert(logfont->lfFaceName);
    return 1;
}

int CALLBACK collectFace(const LOGFONTW *logfont, const TEXTMETRICW *metrics, DWORD,
                         LPARAM context) {
    auto *found = reinterpret_cast<std::vector<FontCandidate> *>(context);
    if ((metrics->tmPitchAndFamily & TMPF_FIXED_PITCH) != 0)
        return 1;
    if (logfont->lfFaceName[0] == L'@')
        return 1;
    // EnumFontFamiliesEx passes an ENUMLOGFONTEXW, whose elfStyle names the face ("Bold Italic").
    const auto *extended = reinterpret_cast<const ENUMLOGFONTEXW *>(logfont);
    FontCandidate candidate;
    candidate.family = toUtf8(logfont->lfFaceName);
    candidate.style = toUtf8(extended->elfStyle);
    candidate.weight = static_cast<int>(logfont->lfWeight);
    candidate.italic = logfont->lfItalic != 0;
    candidate.fixed_pitch = true;
    found->push_back(std::move(candidate));
    return 1;
}

} // namespace

std::vector<FontCandidate> enumerateSystemFonts() {
    std::vector<FontCandidate> found;
    HDC dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr)
        return found;

    std::set<std::wstring> names;
    LOGFONTW request{};
    request.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &request, collectFamily, reinterpret_cast<LPARAM>(&names), 0);
    for (const std::wstring &name : names) {
        LOGFONTW per_family{};
        per_family.lfCharSet = DEFAULT_CHARSET;
        std::wcscpy(per_family.lfFaceName, name.c_str());
        EnumFontFamiliesExW(dc, &per_family, collectFace, reinterpret_cast<LPARAM>(&found), 0);
    }
    DeleteDC(dc);

    // EnumFontFamiliesEx reports a face once per character set; probe each face once.
    std::set<std::tuple<std::string, std::string, int, bool>> seen;
    std::vector<FontCandidate> unique;
    for (FontCandidate &candidate : found) {
        if (!seen.insert({candidate.family, candidate.style, candidate.weight, candidate.italic})
                 .second)
            continue;
        candidate.loadable =
            readFace(candidate.family, candidate.weight, candidate.italic, nullptr);
        unique.push_back(std::move(candidate));
    }
    return unique;
}

bool readSystemFontFace(const std::string &family, const FontFace &face,
                        std::vector<unsigned char> &bytes) {
    std::vector<unsigned char> data;
    if (!readFace(family, face.weight, face.italic, &data))
        return false;
    bytes = std::move(data);
    return true;
}

} // namespace ui_font
