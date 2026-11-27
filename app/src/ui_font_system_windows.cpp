#include "ui_font_model.h"

#include <windows.h>

#include <cstring>
#include <set>
#include <string>
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

// Reads the whole file of the face GDI resolves for `family`. GDI substitutes another face for
// an unknown name, so the face it reports must match the request or the read is refused.
bool readFaceBytes(const std::string &family, std::vector<unsigned char> &bytes) {
    const std::wstring wide = toWide(family);
    if (wide.empty() || wide.size() >= LF_FACESIZE)
        return false;

    HDC dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr)
        return false;

    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfWeight = FW_NORMAL;
    std::wcscpy(lf.lfFaceName, wide.c_str());
    HFONT font = CreateFontIndirectW(&lf);
    bool ok = false;
    if (font != nullptr) {
        HGDIOBJ previous = SelectObject(dc, font);
        wchar_t face[LF_FACESIZE] = {};
        if (GetTextFaceW(dc, LF_FACESIZE, face) > 0 && _wcsicmp(face, wide.c_str()) == 0) {
            const DWORD size = GetFontData(dc, 0, 0, nullptr, 0);
            if (size != GDI_ERROR && size > 0) {
                std::vector<unsigned char> data(size);
                if (GetFontData(dc, 0, 0, data.data(), size) == size &&
                    isSingleFaceFontFile(data)) {
                    bytes = std::move(data);
                    ok = true;
                }
            }
        }
        SelectObject(dc, previous);
        DeleteObject(font);
    }
    DeleteDC(dc);
    return ok;
}

int CALLBACK collectFamily(const LOGFONTW *logfont, const TEXTMETRICW *metrics, DWORD,
                           LPARAM context) {
    auto *found = reinterpret_cast<std::vector<FontCandidate> *>(context);
    // TMPF_FIXED_PITCH is set for variable-pitch faces (TEXTMETRIC documentation), so a
    // fixed-pitch face has the bit clear.
    if ((metrics->tmPitchAndFamily & TMPF_FIXED_PITCH) != 0)
        return 1;
    if (logfont->lfFaceName[0] == L'@')
        return 1;
    FontCandidate candidate;
    candidate.family = toUtf8(logfont->lfFaceName);
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
    LOGFONTW request{};
    request.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &request, collectFamily, reinterpret_cast<LPARAM>(&found), 0);
    DeleteDC(dc);

    // EnumFontFamiliesEx reports a family once per character set; read each name once.
    std::set<std::string> seen;
    std::vector<FontCandidate> unique;
    for (FontCandidate &candidate : found) {
        if (!seen.insert(candidate.family).second)
            continue;
        std::vector<unsigned char> probe;
        candidate.loadable = readFaceBytes(candidate.family, probe);
        unique.push_back(std::move(candidate));
    }
    return unique;
}

bool readSystemFontFile(const std::string &family, std::vector<unsigned char> &bytes) {
    return readFaceBytes(family, bytes);
}

} // namespace ui_font
