#include "ui_font_model.h"

#include <dwrite.h>
#include <windows.h>

#include <cstdio>
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

// Releases a COM interface when it leaves scope. put() gives the address for an out parameter.
template <typename T> class ComPtr {
  public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;

    void reset() {
        if (ptr_ != nullptr) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }
    T **put() {
        reset();
        return &ptr_;
    }
    T *get() const { return ptr_; }
    T *operator->() const { return ptr_; }

  private:
    T *ptr_ = nullptr;
};

std::string toUtf8(const std::wstring &wide) {
    if (wide.empty())
        return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0)
        return {};
    std::string utf8(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), length,
                        nullptr, nullptr);
    return utf8;
}

// Reads a whole file by its wide path. Returns false when it cannot be opened or read.
bool readWholeFile(const std::string &path, std::vector<unsigned char> &bytes) {
    FILE *file = _wfopen(toWide(path).c_str(), L"rb");
    if (file == nullptr)
        return false;
    std::vector<unsigned char> data;
    unsigned char chunk[65536];
    std::size_t count = 0;
    while ((count = std::fread(chunk, 1, sizeof(chunk), file)) > 0)
        data.insert(data.end(), chunk, chunk + count);
    const bool ok = std::ferror(file) == 0;
    std::fclose(file);
    if (!ok)
        return false;
    bytes = std::move(data);
    return true;
}

// The style name DirectWrite gives the face, in the first language the font names it in.
std::string faceStyle(IDWriteFont *font) {
    ComPtr<IDWriteLocalizedStrings> names;
    if (FAILED(font->GetFaceNames(names.put())) || names.get() == nullptr)
        return {};
    UINT32 length = 0;
    if (FAILED(names->GetStringLength(0, &length)))
        return {};
    std::wstring style(static_cast<std::size_t>(length) + 1, L'\0');
    if (FAILED(names->GetString(0, &style[0], length + 1)))
        return {};
    style.resize(length);
    return toUtf8(style);
}

// The file a face is read from. Empty when the face is not in a single file DirectWrite exposes
// through the local loader (a font collection, for example).
std::string faceFile(IDWriteFont *font) {
    ComPtr<IDWriteFontFace> face;
    if (FAILED(font->CreateFontFace(face.put())) || face.get() == nullptr)
        return {};
    UINT32 count = 0;
    if (FAILED(face->GetFiles(&count, nullptr)) || count != 1)
        return {};
    ComPtr<IDWriteFontFile> file;
    if (FAILED(face->GetFiles(&count, file.put())) || file.get() == nullptr)
        return {};
    ComPtr<IDWriteFontFileLoader> loader;
    if (FAILED(file->GetLoader(loader.put())) || loader.get() == nullptr)
        return {};
    ComPtr<IDWriteLocalFontFileLoader> local;
    if (FAILED(loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader),
                                      reinterpret_cast<void **>(local.put()))) ||
        local.get() == nullptr)
        return {};
    const void *key = nullptr;
    UINT32 key_size = 0;
    if (FAILED(file->GetReferenceKey(&key, &key_size)))
        return {};
    UINT32 path_length = 0;
    if (FAILED(local->GetFilePathLengthFromKey(key, key_size, &path_length)) || path_length == 0)
        return {};
    std::wstring path(static_cast<std::size_t>(path_length) + 1, L'\0');
    if (FAILED(local->GetFilePathFromKey(key, key_size, &path[0], path_length + 1)))
        return {};
    path.resize(path_length);
    return toUtf8(path);
}

// Whether the first bytes of `path` begin a single-face font (checked without reading the file).
bool isSingleFaceFile(const std::string &path) {
    FILE *file = _wfopen(toWide(path).c_str(), L"rb");
    if (file == nullptr)
        return false;
    unsigned char head[4] = {};
    const bool ok = std::fread(head, 1, sizeof(head), file) == sizeof(head);
    std::fclose(file);
    return ok && isSingleFaceFontFile(std::vector<unsigned char>(head, head + sizeof(head)));
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
