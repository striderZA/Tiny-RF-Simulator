#include "ui_font_model.h"

#include <dwrite.h>
#include <windows.h>

#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Families come from GDI; each family's faces come from DirectWrite, which names them and gives
// their files.

namespace ui_font {
namespace {

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

// Opens a font file for ranged reads. The handle stays open until the last reader is dropped.
FileSource openFontFile(const std::string &path) {
    FileSource source;
    std::shared_ptr<FILE> file(_wfopen(toWide(path).c_str(), L"rb"), [](FILE *handle) {
        if (handle != nullptr)
            std::fclose(handle);
    });
    if (!file || _fseeki64(file.get(), 0, SEEK_END) != 0)
        return source;
    const long long size = _ftelli64(file.get());
    if (size <= 0)
        return source;
    source.open = true;
    source.size = static_cast<std::size_t>(size);
    source.read = [file](std::size_t offset, std::size_t length) {
        std::vector<unsigned char> bytes(length);
        if (_fseeki64(file.get(), static_cast<long long>(offset), SEEK_SET) != 0)
            return std::vector<unsigned char>();
        bytes.resize(std::fread(bytes.data(), 1, length, file.get()));
        return bytes;
    };
    return source;
}

// Collects the monospaced family names. EnumFontFamiliesEx with an empty face name reports one
// face per family, so the names are gathered once and each is then read through DirectWrite.
int CALLBACK collectFamily(const LOGFONTW *logfont, const TEXTMETRICW *metrics, DWORD,
                           LPARAM context) {
    auto *names = reinterpret_cast<std::set<std::wstring> *>(context);
    // TMPF_FIXED_PITCH is set for variable-pitch faces (TEXTMETRIC documentation), so a
    // fixed-pitch face has the bit clear.
    if ((metrics->tmPitchAndFamily & TMPF_FIXED_PITCH) == 0 && logfont->lfFaceName[0] != L'@')
        names->insert(logfont->lfFaceName);
    return 1;
}

} // namespace

std::vector<FontCandidate> enumerateSystemFonts() {
    std::vector<FontCandidate> found;
    std::set<std::wstring> names;
    HDC dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr)
        return found;
    LOGFONTW request{};
    request.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &request, collectFamily, reinterpret_cast<LPARAM>(&names), 0);
    DeleteDC(dc);

    ComPtr<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown **>(factory.put()))) ||
        factory.get() == nullptr)
        return found;
    ComPtr<IDWriteFontCollection> collection;
    if (FAILED(factory->GetSystemFontCollection(collection.put(), FALSE)) ||
        collection.get() == nullptr)
        return found;

    for (const std::wstring &name : names) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (FAILED(collection->FindFamilyName(name.c_str(), &index, &exists)) || !exists)
            continue;
        ComPtr<IDWriteFontFamily> family;
        if (FAILED(collection->GetFontFamily(index, family.put())) || family.get() == nullptr)
            continue;
        const UINT32 count = family->GetFontCount();
        for (UINT32 i = 0; i < count; ++i) {
            ComPtr<IDWriteFont> font;
            if (FAILED(family->GetFont(i, font.put())) || font.get() == nullptr)
                continue;
            FontCandidate candidate;
            candidate.family = toUtf8(name);
            candidate.style = faceStyle(font.get());
            candidate.weight = static_cast<int>(font->GetWeight());
            candidate.italic = font->GetStyle() == DWRITE_FONT_STYLE_ITALIC;
            candidate.fixed_pitch = true;
            candidate.file = faceFile(font.get());
            found.push_back(std::move(candidate));
        }
    }
    identifyFiles(found, openFontFile);
    return found;
}

bool readSystemFontFace(const std::string &family, const FontFace &face,
                        std::vector<unsigned char> &bytes) {
    (void)family; // the file names the face; the family only names it for the caller
    if (face.file.empty())
        return false;
    std::vector<unsigned char> data;
    if (!readWholeFile(face.file, data) || !isSingleFaceFontFile(data))
        return false;
    bytes = std::move(data);
    return true;
}

} // namespace ui_font
