#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

// UI-free rules for the Settings font picker: which installed families are offered, which faces a
// family has, how the saved choices resolve against them, and the platform reads that feed both.
// ImGui code lives in ui_font_controller.cpp; nothing here includes imgui.h.
namespace ui_font {

// One installed face as the operating system reports it. A family has one candidate per face.
struct FontCandidate {
    std::string family;
    std::string style;        // the face's style name, e.g. "Bold Italic"; may be empty
    int weight = 400;         // OpenType weight class, 100-900
    bool italic = false;      // an italic face; on Windows an oblique is reported upright
    bool fixed_pitch = false; // a monospaced (fixed-pitch) face
    bool loadable = false;    // readable as one TrueType or OpenType face
    std::string file; // the file the face is read from, as the system reports it; may be empty
    std::string content_key; // identifies the file's bytes (fileIdentity()); empty when not read
    int default_weight = 0;  // a variable file's default instance weight (fileIdentity()); else 0
};

// A face of one family, as the style list offers it.
struct FontFace {
    std::string style;
    int weight = 400;
    bool italic = false;
    std::string file; // the file the face is read from; empty where the platform reports none
};

// The style a choice asks for. An empty name is Regular.
struct StyleChoice {
    std::string name;
    int weight = 400;
    bool italic = false;
};

// The face the emphasis slot (bold text) is filled from.
enum class Emphasis { Bold, Medium };

// Where the saved family landed. An empty applied_family is the built-in Default font.
struct FontResolution {
    std::string applied_family;
    std::string missing_family; // the saved name when it is not installed, else empty
};

// The faces chosen for one family. `notes` names every fallback in words for the Settings window.
struct FacePlan {
    bool has_face = false; // false when the family has no loadable face
    FontFace body;         // the face for regular text
    FontFace emphasis;     // the face for emphasis text; equal to body when there is none
    std::vector<std::string> notes;
};

// Supported UI font sizes, in pixels at 100% scale.
inline constexpr int kMinSizePx = 10;
inline constexpr int kMaxSizePx = 24;

// A saved size. size_px is 0 for the built-in size; a note says what was changed.
struct SizeResolution {
    int size_px = 0;
    std::string note;
};

// A saved emphasis. A value that is not Bold or Medium resolves to Bold with a note.
struct EmphasisResolution {
    Emphasis emphasis = Emphasis::Bold;
    std::string note;
};

// The families the picker lists: fixed-pitch, loadable, not a vertical '@' face, one entry per
// family name (compared case-insensitively), sorted case-insensitively. Default is not included.
std::vector<std::string> monospacedFamilies(const std::vector<FontCandidate> &candidates);

// The loadable faces of `family` among `candidates`, one row per face the family draws. Candidates
// that read the same bytes draw one face: a variable file's named weights, and a second installed
// copy of a file. Such a group is labelled by the weight its file draws by default, so a variable
// file shows its default instance (Regular 400 for a font that declares it), never its lightest
// named weight. Where no bytes are known, faces that share a file are one row, then weight and
// slant decide. Upright faces come first, each group by ascending weight. A style name repeated
// within the family is labelled with its weight, so every face has a distinct label.
std::vector<FontFace> familyFaces(const std::vector<FontCandidate> &candidates,
                                  const std::string &family);

// Resolves the saved family. An empty `saved` is Default. A name in `installed` matches
// case-insensitively and resolves to the installed spelling. Any other name falls back to Default
// and is reported in missing_family; the caller keeps the saved name so it returns when the font
// is installed again.
FontResolution resolveSavedFamily(const std::string &saved,
                                  const std::vector<std::string> &installed);

// The faces for a family given the chosen style and emphasis. A saved style matches a face by name,
// and by weight and slant when the name repeats. A style the family lacks resolves to the nearest
// face (same slant first, then nearest weight) and says so in notes. A family without a Bold face
// uses its Regular face for emphasis, and a family without a Medium face uses Bold.
FacePlan planFaces(const std::string &family, const std::vector<FontFace> &faces,
                   const StyleChoice &style, Emphasis emphasis);

// Resolves a saved size: an empty string is the built-in size; a whole number outside
// kMinSizePx..kMaxSizePx is clamped to the nearest limit with a note.
SizeResolution resolveSavedSize(const std::string &saved);

// Resolves a saved emphasis name ("Bold" or "Medium", case-insensitive). Empty is Bold.
EmphasisResolution resolveSavedEmphasis(const std::string &saved);

// The saved name of an emphasis value.
std::string emphasisName(Emphasis emphasis);

// True when the family has an upright face for the emphasis slot: Bold (nearest 700) or Medium
// (nearest 500), within the same windows planFaces() uses.
bool offersEmphasis(const std::vector<FontFace> &faces, Emphasis emphasis);

// True when `bytes` begins with a single-face TrueType (0x00010000, 'true') or OpenType CFF
// ('OTTO') header. Font collections ('ttcf'), bitmap .fon/.pcf files, and anything else are not.
bool isSingleFaceFontFile(const std::vector<unsigned char> &bytes);

// The identity of a single-face font file's bytes. `content_key` is equal for identical files
// (64-bit FNV-1a with the size). `default_weight` is the wght default of a variable font's fvar
// table, the weight its default instance draws; 0 for a static font, whose platform name stands.
struct FileIdentity {
    std::string content_key;
    int default_weight = 0;
};
FileIdentity fileIdentity(const std::vector<unsigned char> &bytes);

// Reads `length` bytes of a font file from `offset`. Returns fewer bytes at the end of the file.
using ByteReader =
    std::function<std::vector<unsigned char>(std::size_t offset, std::size_t length)>;

// A font file the platform can read: whether it opened, its size, and a reader over its bytes.
struct FileSource {
    bool open = false;
    std::size_t size = 0;
    ByteReader read;
};

// Opens the font file at a path the system reports.
using FileOpener = std::function<FileSource(const std::string &path)>;

// Sets `loadable`, `default_weight`, and `content_key` on every candidate that names a file. Each
// file is opened once. Only the small tables are read for the default weight; the whole file is
// hashed only where another file of the same family has the same size, since identical files
// have equal sizes. Candidates with no file are left as they are.
void identifyFiles(std::vector<FontCandidate> &candidates, const FileOpener &open);

// Monospaced faces as the operating system reports them, unfiltered beyond what the platform
// enumeration itself requires. Empty where the platform has no enumeration.
std::vector<FontCandidate> enumerateSystemFonts();

// Reads the single-face file of `face`. Where the platform reports the face's own file (Windows),
// that file is read and `family` only names the face for the caller. Where it does not
// (fontconfig), the family's face at `face`'s weight and slant is resolved, and the read fails when
// the system substitutes a different face. Returns false when the file is not a single-face font.
// `bytes` is replaced only on success.
bool readSystemFontFace(const std::string &family, const FontFace &face,
                        std::vector<unsigned char> &bytes);

} // namespace ui_font
