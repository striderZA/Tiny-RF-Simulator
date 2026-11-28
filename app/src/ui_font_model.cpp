#include "ui_font_model.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
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

// Big-endian reads from a font file. Each fails, and leaves `out` alone, when it would run past the
// end.
bool readU16(const std::vector<unsigned char> &bytes, std::size_t at, std::uint32_t &out) {
    if (at + 2 > bytes.size())
        return false;
    out = (static_cast<std::uint32_t>(bytes[at]) << 8) | bytes[at + 1];
    return true;
}

bool readU32(const std::vector<unsigned char> &bytes, std::size_t at, std::uint32_t &out) {
    if (at + 4 > bytes.size())
        return false;
    out = (static_cast<std::uint32_t>(bytes[at]) << 24) |
          (static_cast<std::uint32_t>(bytes[at + 1]) << 16) |
          (static_cast<std::uint32_t>(bytes[at + 2]) << 8) | bytes[at + 3];
    return true;
}

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

std::string styleNameOf(const FontCandidate &candidate) {
    return candidate.style.empty() ? styleNameFor(candidate.weight, candidate.italic)
                                   : candidate.style;
}

// The face a group of candidates that read the same bytes draws. The file draws its default
// instance, so the row is the candidate at the file's default weight (upright first); when the
// platform does not list that weight, the row takes the default weight and its name from the
// weight. Where the default weight is unknown, the candidate nearest Regular is the row.
FontFace drawnFace(const std::vector<const FontCandidate *> &members) {
    int target = 0;
    for (const FontCandidate *member : members)
        if (member->default_weight > 0) {
            target = member->default_weight;
            break;
        }
    const bool known = target > 0;
    if (!known)
        target = kRegularWeight;
    const FontCandidate *best = members.front();
    const auto rank = [target](const FontCandidate *c) {
        return std::make_tuple(c->weight != target, c->italic, std::abs(c->weight - target),
                               c->weight, styleNameOf(*c) != "Regular", styleNameOf(*c));
    };
    for (const FontCandidate *member : members)
        if (rank(member) < rank(best))
            best = member;
    FontFace face;
    face.file = best->file;
    face.italic = best->italic;
    if (known && best->weight != target) {
        face.weight = target;
        face.style = styleNameFor(target, best->italic);
    } else {
        face.weight = best->weight;
        face.style = styleNameOf(*best);
    }
    return face;
}

// An SFNT table's place in the file.
struct TableEntry {
    std::size_t offset = 0;
    std::size_t length = 0;
};

// The directory entry for `tag`, when the table lies inside a file of `size` bytes.
bool findTable(std::size_t size, const ByteReader &read, const char *tag, TableEntry &entry) {
    const std::vector<unsigned char> count_bytes = read(4, 2);
    std::uint32_t count = 0;
    if (!readU16(count_bytes, 0, count))
        return false;
    const std::vector<unsigned char> records = read(12, 16 * static_cast<std::size_t>(count));
    for (std::size_t i = 0; i + 16 <= records.size(); i += 16) {
        if (std::memcmp(&records[i], tag, 4) != 0)
            continue;
        std::uint32_t offset = 0;
        std::uint32_t length = 0;
        if (!readU32(records, i + 8, offset) || !readU32(records, i + 12, length))
            return false;
        if (static_cast<std::size_t>(offset) + length > size)
            return false;
        entry.offset = offset;
        entry.length = length;
        return true;
    }
    return false;
}

// The weight a variable font draws by default: the wght default of its fvar table (the default
// instance). A static font declares no default instance and returns 0, so its platform name stands.
int drawnWeightOf(std::size_t size, const ByteReader &read) {
    constexpr std::size_t kLargestTable = std::size_t{1} << 20;
    TableEntry fvar;
    if (findTable(size, read, "fvar", fvar) && fvar.length >= 16 && fvar.length <= kLargestTable) {
        const std::vector<unsigned char> table = read(fvar.offset, fvar.length);
        std::uint32_t axes_offset = 0;
        std::uint32_t axis_count = 0;
        std::uint32_t axis_size = 0;
        if (readU16(table, 4, axes_offset) && readU16(table, 8, axis_count) &&
            readU16(table, 10, axis_size) && axis_size >= 12) {
            for (std::uint32_t a = 0; a < axis_count; ++a) {
                const std::size_t axis = axes_offset + static_cast<std::size_t>(a) * axis_size;
                if (axis + 12 > table.size())
                    break;
                if (std::memcmp(&table[axis], "wght", 4) != 0)
                    continue;
                std::uint32_t raw = 0; // 16.16 fixed point
                if (readU32(table, axis + 8, raw)) {
                    const int weight = static_cast<int>(static_cast<std::int32_t>(raw) >> 16);
                    if (weight >= 1 && weight <= 1000)
                        return weight;
                }
                break;
            }
        }
    }
    return 0;
}

// A 64-bit FNV-1a hash of the whole file, with its size. Empty when a read comes up short.
std::string contentKeyOf(std::size_t size, const ByteReader &read) {
    constexpr std::size_t kChunk = std::size_t{1} << 16;
    std::uint64_t hash = 14695981039346656037ULL;
    std::size_t offset = 0;
    while (offset < size) {
        const std::vector<unsigned char> chunk = read(offset, kChunk);
        if (chunk.empty())
            return {};
        for (unsigned char byte : chunk) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
        offset += chunk.size();
    }
    char key[64];
    std::snprintf(key, sizeof(key), "%016llx:%zu", static_cast<unsigned long long>(hash), size);
    return key;
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
        return std::make_tuple(a->italic, a->weight, name_a != "Regular", name_a, a->file) <
               std::make_tuple(b->italic, b->weight, name_b != "Regular", name_b, b->file);
    });
    // A face is one file. A file that holds several named weights (a variable font) draws its
    // default face for each, so it is one row. Where the platform reports no file (fontconfig), the
    // weight and slant decide identity. A second file that carries the same face shows once.
    std::set<std::string> seen_file;
    std::set<std::tuple<int, bool>> seen_slot;
    std::set<std::tuple<std::string, int, bool>> seen_face;
    std::vector<FontFace> faces;
    for (const FontCandidate *candidate : matches) {
        const auto face_key =
            std::make_tuple(styleNameOf(*candidate), candidate->weight, candidate->italic);
        const bool duplicate = candidate->file.empty()
                                   ? seen_slot.count({candidate->weight, candidate->italic}) != 0
                                   : seen_file.count(candidate->file) != 0;
        if (duplicate || seen_face.count(face_key) != 0)
            continue;
        if (candidate->file.empty())
            seen_slot.insert({candidate->weight, candidate->italic});
        else
            seen_file.insert(candidate->file);
        seen_face.insert(face_key);
        FontFace face;
        face.style = styleNameOf(*candidate);
        face.weight = candidate->weight;
        face.italic = candidate->italic;
        face.file = candidate->file;
        faces.push_back(std::move(face));
    }
    // A name repeated within the family is labelled with its weight, so each row names one face. A
    // label that still repeats gets an ordinal, so every row is distinct.
    std::map<std::string, int> uses;
    for (const FontFace &face : faces)
        ++uses[face.style];
    for (FontFace &face : faces)
        if (uses[face.style] > 1)
            face.style += " (" + std::to_string(face.weight) + ")";
    std::map<std::string, int> ordinal;
    for (FontFace &face : faces) {
        const int count = ++ordinal[face.style];
        if (count > 1)
            face.style += " " + std::to_string(count);
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

FacePlan planFaces(const std::string &family, const std::vector<FontFace> &faces,
                   const StyleChoice &style, Emphasis emphasis) {
    FacePlan plan;
    if (faces.empty())
        return plan;
    plan.has_face = true;

    const FontFace regular = regularFace(faces);
    if (style.name.empty()) {
        plan.body = regular;
    } else {
        const std::string key = foldCase(style.name);
        // A name matches the face with the saved weight and slant when the name repeats.
        const FontFace *named = nullptr;
        for (const FontFace &face : faces) {
            if (foldCase(face.style) == key && face.weight == style.weight &&
                face.italic == style.italic) {
                named = &face;
                break;
            }
        }
        if (named == nullptr) {
            for (const FontFace &face : faces) {
                if (foldCase(face.style) == key) {
                    named = &face;
                    break;
                }
            }
        }
        if (named != nullptr) {
            plan.body = *named;
        } else {
            plan.body = nearestStyle(faces, style);
            plan.notes.push_back("Style " + quoted(style.name) + " is not available in " +
                                 quoted(family) + ". Showing " + quoted(plan.body.style) + ".");
        }
    }

    FontFace bold;
    const bool has_bold = nearestFace(faces, false, kBoldWeight, kBoldWindow, bold);
    FontFace medium;
    const bool has_medium = nearestFace(faces, false, kMediumWeight, kMediumWindow, medium);
    if (emphasis == Emphasis::Medium && has_medium) {
        plan.emphasis = medium;
        return plan;
    }
    if (emphasis == Emphasis::Medium)
        plan.notes.push_back(quoted(family) + " has no Medium face, so bold text uses " +
                             quoted(has_bold ? bold.style : regular.style) + ".");
    if (has_bold) {
        plan.emphasis = bold;
    } else {
        plan.emphasis = regular;
        plan.notes.push_back(quoted(family) + " has no Bold face, so bold text uses its " +
                             quoted(regular.style) + " face.");
    }
    return plan;
}

SizeResolution resolveSavedSize(const std::string &saved) {
    SizeResolution resolution;
    if (saved.empty())
        return resolution;
    int value = 0;
    const char *first = saved.data();
    const char *last = saved.data() + saved.size();
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ec != std::errc() || parsed.ptr != last) {
        resolution.note = "Saved font size " + quoted(saved) +
                          " is not a whole number of pixels. Using the built-in size.";
        return resolution;
    }
    if (value < kMinSizePx || value > kMaxSizePx) {
        resolution.size_px = std::clamp(value, kMinSizePx, kMaxSizePx);
        resolution.note = "Saved font size " + std::to_string(value) + " px is outside " +
                          std::to_string(kMinSizePx) + "-" + std::to_string(kMaxSizePx) +
                          " px. Using " + std::to_string(resolution.size_px) + " px.";
        return resolution;
    }
    resolution.size_px = value;
    return resolution;
}

EmphasisResolution resolveSavedEmphasis(const std::string &saved) {
    EmphasisResolution resolution;
    if (saved.empty())
        return resolution;
    if (foldCase(saved) == "medium") {
        resolution.emphasis = Emphasis::Medium;
        return resolution;
    }
    if (foldCase(saved) == "bold")
        return resolution;
    resolution.note = "Saved emphasis " + quoted(saved) + " is not Bold or Medium. Using Bold.";
    return resolution;
}

std::string emphasisName(Emphasis emphasis) {
    return emphasis == Emphasis::Medium ? "Medium" : "Bold";
}

bool offersEmphasis(const std::vector<FontFace> &faces, Emphasis emphasis) {
    FontFace face;
    if (emphasis == Emphasis::Medium)
        return nearestFace(faces, false, kMediumWeight, kMediumWindow, face);
    return nearestFace(faces, false, kBoldWeight, kBoldWindow, face);
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
