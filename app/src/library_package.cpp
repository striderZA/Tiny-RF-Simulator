#include "library_package.h"

#include "component_library.h"
#include "logging_core.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include <miniz.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

// --- Path containment (mirrors component_library.cpp's resolveDataFilePath) --

bool containsParentTraversal(const fs::path &path) {
    for (const auto &part : path) {
        if (part == "..")
            return true;
    }
    return false;
}

bool pathWithinRoot(const fs::path &root, const fs::path &candidate) {
    std::error_code ec;
    const fs::path canonical_root = fs::weakly_canonical(root, ec);
    if (ec)
        return false;

    ec.clear();
    const fs::path canonical_candidate = fs::weakly_canonical(candidate, ec);
    if (ec)
        return false;

    auto root_it = canonical_root.begin();
    auto candidate_it = canonical_candidate.begin();
    for (; root_it != canonical_root.end(); ++root_it, ++candidate_it) {
        if (candidate_it == canonical_candidate.end() || *root_it != *candidate_it)
            return false;
    }
    return true;
}

// Same discipline as ComponentLibrary's resolveDataFilePath(): the reference is
// resolved against its JSON directory and only honored when its canonical form
// stays inside that directory. Export is strict where the loader is lenient:
// an unresolvable or escaping reference aborts the whole package instead of
// being skipped.
std::optional<fs::path> resolveContained(const fs::path &json_dir, const std::string &input) {
    const fs::path p(input);
    if (p.empty())
        return std::nullopt;
    if (containsParentTraversal(p))
        return std::nullopt;

    const fs::path candidate = p.is_absolute() ? p : (json_dir / p);
    if (!pathWithinRoot(json_dir, candidate))
        return std::nullopt;

    std::error_code ec;
    const fs::path resolved = fs::weakly_canonical(candidate, ec);
    if (ec)
        return std::nullopt;
    return resolved;
}

// --- Slash-separated, lexically normal relative paths ------------------------

// Archives must carry forward-slash member names on every platform, and the
// manifest/definition spellings must match what an importer resolves relative
// to another JSON directory. A leading "./" is not part of the spelling.
std::string toSlash(const fs::path &path) {
    std::string s = path.generic_string();
    while (s.rfind("./", 0) == 0)
        s.erase(0, 2);
    return s;
}

std::string toLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// The canonical target validates containment and supplies bytes, but package
// names must follow the lexical path the author referenced (not a symlink's
// target name).
std::optional<fs::path> authoredPathRelativeTo(const fs::path &base, const std::string &reference) {
    const fs::path authored(reference);
    const fs::path relative = authored.is_absolute()
                                  ? authored.lexically_normal().lexically_relative(base)
                                  : authored.lexically_normal();
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
        relative.has_root_directory() || containsParentTraversal(relative))
        return std::nullopt;
    return relative;
}

std::string zipError(const mz_zip_archive &zip) {
    return mz_zip_get_error_string(mz_zip_get_last_error(const_cast<mz_zip_archive *>(&zip)));
}

// A definition plus its packaged members, assembled before anything is written
// so every refusal path leaves no partial archive behind.
struct ExportEntry {
    std::string archive_name; // library/<source-relative>
    std::string source_path;  // canonical on-disk JSON path
    std::string content;      // packaged definition JSON (dump(2))
    // Identity as the loader parsed it; the manifest repeats these so an
    // importer's conflict detection sees exactly what the library sees.
    std::string type;
    std::string manufacturer;
    std::string part_number;
    struct Asset {
        std::string archive_name;  // library/<source-relative>
        std::string source_path;   // on-disk file to stream into the archive
        std::string raw_reference; // spelling as authored
    };
    std::vector<Asset> assets;
};

bool walkRoot(const fs::path &source_root, std::vector<fs::path> &json_files, std::string &error) {
    std::error_code ec;
    if (!fs::exists(source_root, ec) || !fs::is_directory(source_root, ec)) {
        error = "library root does not exist: " + source_root.string();
        return false;
    }

    fs::recursive_directory_iterator it(source_root, fs::directory_options::skip_permission_denied,
                                        ec);
    if (ec) {
        error = "cannot read library root: " + source_root.string();
        return false;
    }
    const fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            error = "cannot read library root: " + source_root.string();
            return false;
        }
        std::error_code type_ec;
        if (!it->is_regular_file(type_ec) || type_ec)
            continue;
        if (it->path().extension() != ".json")
            continue;
        json_files.push_back(it->path());
    }

    std::sort(json_files.begin(), json_files.end());
    return true;
}

// Every reference a definition may carry: data_files entries plus the two
// path-bearing parameter keys (the same keys ComponentLibrary::instantiate
// resolves).
std::vector<const char *> referenceParameterKeys() { return {"sparam_filepath", "sparam_path"}; }

bool collectEntry(const fs::path &source_root, const fs::path &json_path, ExportEntry &entry,
                  std::string &error) {
    std::error_code ec;
    const fs::path canonical_json = fs::canonical(json_path, ec);
    if (ec) {
        error = "cannot resolve library definition: " + json_path.string();
        return false;
    }
    if (!pathWithinRoot(source_root, canonical_json)) {
        error = "library definition resolves outside the selected root: " + json_path.string();
        return false;
    }
    const fs::path json_dir = canonical_json.parent_path();

    // Keep the in-root directory entry spelling in the archive, but only after
    // checking that a symlink did not redirect the JSON outside the chosen root.
    const fs::path relative_json = json_path.lexically_relative(source_root);
    if (relative_json.empty() || relative_json.is_absolute() || relative_json.has_root_name() ||
        relative_json.has_root_directory() || containsParentTraversal(relative_json)) {
        error = "cannot derive a safe package path for: " + json_path.string();
        return false;
    }

    // Load through the exact production loader contract in an isolated
    // instance: anything that does not yield exactly one definition is
    // malformed for packaging purposes (dropped by loadFile(), or split into
    // several entries by a future loader change).
    ComponentLibrary probe;
    probe.loadFile(json_path.string());
    const auto definitions = probe.all();
    if (definitions.size() != 1) {
        error = "malformed component definition (loads as " + std::to_string(definitions.size()) +
                " definition(s)): " + json_path.string();
        return false;
    }
    const ComponentDefinition &def = *definitions.front();
    entry.source_path = canonical_json.string();
    entry.type = def.type;
    entry.manufacturer = def.manufacturer;
    entry.part_number = def.part_number;

    entry.archive_name = "library/" + toSlash(relative_json);

    // Parse the raw file: the packaged JSON is the parsed object dumped with
    // dump(2), so the absolute-to-relative rewrite is uniform and every
    // unmodeled field survives the round trip.
    json raw;
    {
        std::ifstream ifs(json_path);
        if (!ifs.is_open()) {
            error = "cannot open library file: " + json_path.string();
            return false;
        }
        try {
            ifs >> raw;
        } catch (const json::exception &e) {
            error = std::string("malformed component definition (") + e.what() +
                    "): " + json_path.string();
            return false;
        }
    }

    std::map<std::string, std::string> seen_references;

    const auto addAsset = [&](const std::string &raw_reference) {
        const auto resolved = resolveContained(json_dir, raw_reference);
        if (!resolved) {
            error = "referenced data file '" + raw_reference + "' for '" + def.part_number + "' (" +
                    json_path.string() + ") is missing or escapes the definition directory";
            return false;
        }
        std::error_code exists_ec;
        if (!fs::is_regular_file(*resolved, exists_ec) || exists_ec) {
            error = "referenced data file '" + raw_reference + "' for '" + def.part_number + "' (" +
                    json_path.string() + ") does not exist";
            return false;
        }

        const fs::path authored(raw_reference);
        const fs::path lexical_path =
            (authored.is_absolute() ? authored : json_dir / authored).lexically_normal();
        const fs::path relative_asset = lexical_path.lexically_relative(source_root);
        if (relative_asset.empty() || relative_asset.is_absolute() ||
            relative_asset.has_root_name() || relative_asset.has_root_directory() ||
            containsParentTraversal(relative_asset)) {
            error = "referenced data file '" + raw_reference + "' for '" + def.part_number +
                    "' is outside the library root: " + json_path.string();
            return false;
        }

        const std::string archive_name = "library/" + toSlash(relative_asset);
        const auto [seen, inserted] = seen_references.emplace(archive_name, resolved->string());
        if (!inserted) {
            std::error_code equivalent_ec;
            if (fs::equivalent(seen->second, *resolved, equivalent_ec) && !equivalent_ec)
                return true;
            error = "referenced data files collide at package member '" + archive_name + "' in " +
                    json_path.string();
            return false;
        }

        ExportEntry::Asset asset;
        asset.archive_name = archive_name;
        asset.source_path = resolved->string();
        asset.raw_reference = raw_reference;
        entry.assets.push_back(std::move(asset));
        return true;
    };

    if (raw.contains("data_files") && !raw["data_files"].is_array()) {
        error = "malformed data_files field (expected an array) in " + json_path.string();
        return false;
    }
    // data_files: absolute references are rewritten to the authored lexical
    // path relative to this definition; relative spellings are preserved.
    if (raw.contains("data_files")) {
        for (auto &df : raw["data_files"]) {
            if (!df.is_object() || !df.contains("type") || !df["type"].is_string() ||
                !df.contains("path") || !df["path"].is_string()) {
                error = "malformed data_files entry in " + json_path.string();
                return false;
            }
            const std::string raw_reference = df["path"].get<std::string>();
            if (!addAsset(raw_reference))
                return false;

            const fs::path authored(raw_reference);
            if (authored.is_absolute()) {
                const auto relative = authoredPathRelativeTo(json_dir, raw_reference);
                if (!relative) {
                    error = "cannot derive a safe relative path for '" + raw_reference + "' in " +
                            json_path.string();
                    return false;
                }
                df["path"] = toSlash(*relative);
            }
        }
    }

    // Path-bearing parameters: the packaged copy keeps the authored spelling
    // for relative paths. An absolute path is only rewritten after the same
    // containment check as a data_files entry, so a relative rewrite resolves
    // against the same definition directory.
    if (raw.contains("parameters") && raw["parameters"].is_object()) {
        for (const char *key : referenceParameterKeys()) {
            if (!raw["parameters"].contains(key) || !raw["parameters"][key].is_string())
                continue;
            const std::string raw_reference = raw["parameters"][key].get<std::string>();
            if (!addAsset(raw_reference))
                return false;
            if (fs::path(raw_reference).is_absolute()) {
                const auto relative = authoredPathRelativeTo(json_dir, raw_reference);
                if (!relative) {
                    error = "cannot derive a safe relative path for '" + raw_reference + "' in " +
                            json_path.string();
                    return false;
                }
                raw["parameters"][key] = toSlash(*relative);
            }
        }
    }

    entry.content = raw.dump(2);
    return true;
}

// --- Import helpers ----------------------------------------------------------

// One manifest-listed component, validated before any extraction happens.
struct ManifestComponent {
    std::string json_member; // archive spelling, library/<rel>
    std::string type;
    std::string manufacturer;
    std::string part_number;
    std::vector<std::string> assets; // archive spellings
};

std::string identityKey(const std::string &type, const std::string &manufacturer,
                        const std::string &part_number) {
    return toLowerAscii(type) + '\x1f' + toLowerAscii(manufacturer) + '\x1f' +
           toLowerAscii(part_number);
}

// `library/<rel>` -> `<rel>`. The manifest, the archive members, and staging
// all speak this one spelling so an extracted payload mirrors the source tree
// exactly (and `manifest.json` is never itself staged).
//
// The stripped form is re-validated, not just the archive spelling: a member
// like `library//etc/passwd` (or `library/C:/evil.json`) is harmless as
// spelled but its remainder is rooted, so joining it onto staging would escape
// it. Reject an empty remainder, an absolute/rooted remainder, and parent
// traversal components.
constexpr const char *kPayloadPrefix = "library/";
constexpr std::size_t kPayloadPrefixLength = 8;

std::optional<fs::path> payloadRelative(const std::string &member) {
    if (member.rfind(kPayloadPrefix, 0) != 0 || member.size() <= kPayloadPrefixLength)
        return std::nullopt;

    const std::string remainder = member.substr(kPayloadPrefixLength);
    // A Windows drive root name ("C:") is not a root name under the POSIX path
    // grammar, so reject it explicitly: archive spellings are always relative
    // POSIX-style paths, so a drive prefix is never a legitimate payload path.
    const bool drive_rooted = remainder.size() >= 2 && remainder[1] == ':' &&
                              ((remainder[0] >= 'A' && remainder[0] <= 'Z') ||
                               (remainder[0] >= 'a' && remainder[0] <= 'z'));
    if (drive_rooted)
        return std::nullopt;

    const fs::path relative(remainder);
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
        relative.has_root_directory() || containsParentTraversal(relative))
        return std::nullopt;
    return relative;
}

// Process-unique half of the staging-directory tag; the retry counter appended
// by the caller makes it unique across concurrent imports in one process too.
unsigned long processId() {
#ifdef _WIN32
    return static_cast<unsigned long>(_getpid());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

// A path a ZIP member may carry. The rules are the containment discipline from
// component_library.cpp, applied to archive spellings: no absolute paths, no
// backslashes (a Windows-style separator would resolve differently than the
// forward-slash spelling the manifest validated), no traversal, no empty or
// directory members.
bool isSafeMemberPath(const std::string &member) {
    if (member.empty() || member.back() == '/')
        return false; // empty or a directory entry
    if (member.find('\\') != std::string::npos)
        return false;

    const fs::path path(member);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
        return false;
    return !containsParentTraversal(path);
}

// Removes the staging directory on every return path of importLibraryPackage.
struct StagingGuard {
    fs::path path;

    ~StagingGuard() {
        if (path.empty())
            return;
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

} // namespace

LibraryPackageExportResult exportLibraryPackage(const std::filesystem::path &source_root,
                                                const std::filesystem::path &output_path) {
    LibraryPackageExportResult result;

    if (output_path.empty()) {
        result.error = "no output path given";
        return result;
    }

    std::error_code root_ec;
    const fs::path canonical_source_root = fs::canonical(source_root, root_ec);
    if (root_ec) {
        result.error = "cannot resolve library root: " + source_root.string();
        return result;
    }

    std::vector<fs::path> json_files;
    if (!walkRoot(canonical_source_root, json_files, result.error))
        return result;

    if (json_files.empty()) {
        result.error = "no component definitions found under: " + source_root.string();
        return result;
    }

    std::vector<ExportEntry> entries;
    entries.reserve(json_files.size());
    std::set<std::string> identities; // lowercased type \x1f manufacturer \x1f part_number

    for (const auto &json_path : json_files) {
        ExportEntry entry;
        if (!collectEntry(canonical_source_root, json_path, entry, result.error))
            return result;

        const std::string identity = toLowerAscii(entry.type) + '\x1f' +
                                     toLowerAscii(entry.manufacturer) + '\x1f' +
                                     toLowerAscii(entry.part_number);
        if (!identities.insert(identity).second) {
            result.error = "duplicate component identity '" + entry.type + "/" +
                           entry.manufacturer + "/" + entry.part_number +
                           "' (ambiguous package): " + json_path.string();
            return result;
        }
        entries.push_back(std::move(entry));
    }

    // --- Manifest -----------------------------------------------------------

    const std::string package_name = sanitizePathSegment(output_path.stem().string(), "package");
    json manifest = {
        {"format", library_package::kFormatId}, {"format_version", library_package::kFormatVersion},
        {"package_name", package_name},         {"notes", ""},
        {"components", json::array()},
    };
#ifdef APP_VERSION
    manifest["created_with"] = std::string("RF Simulator ") + APP_VERSION;
#endif

    int asset_count = 0;
    // Exact archive member names, not lowercased spellings: on a
    // case-sensitive host A.s2p and a.s2p are two distinct members and must
    // count as two, so the reported total always matches the archive.
    std::set<std::string> asset_members;
    for (const auto &entry : entries) {
        json component = {{"json", entry.archive_name},
                          {"type", entry.type},
                          {"manufacturer", entry.manufacturer},
                          {"part_number", entry.part_number},
                          {"assets", json::array()}};
        for (const auto &asset : entry.assets) {
            component["assets"].push_back(asset.archive_name);
            if (asset_members.insert(asset.archive_name).second)
                ++asset_count;
        }
        manifest["components"].push_back(std::move(component));
    }

    // --- Write --------------------------------------------------------------

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);

    const mz_bool init_ok = mz_zip_writer_init_file(&zip, output_path.string().c_str(), 0);
    if (init_ok != MZ_TRUE) {
        result.error =
            "cannot create package file '" + output_path.string() + "': " + zipError(zip);
        mz_zip_writer_end(&zip);
        return result;
    }

    const auto fail = [&](const std::string &message) {
        mz_zip_writer_end(&zip); // there is no finalize/end split to unwind here
        std::error_code ec;
        fs::remove(output_path, ec);
        result.ok = false;
        result.error = message;
        return result;
    };

    const std::string manifest_text = manifest.dump(2);
    if (mz_zip_writer_add_mem(&zip, "manifest.json", manifest_text.data(), manifest_text.size(),
                              MZ_BEST_COMPRESSION) != MZ_TRUE) {
        return fail("cannot add the package manifest: " + zipError(zip));
    }

    // One entry per on-disk file, keyed by identity rather than spelling.
    // Hold onto each source path so a later candidate can be compared with
    // fs::equivalent, which is true for two spellings of one file (and on
    // case-insensitive filesystems for two different casings of one name) but
    // false for two genuinely distinct files such as A.s2p and a.s2p where
    // both exist. Collapsing by lowercased spelling instead would drop a
    // case-distinct file the manifest already lists, leaving the archive with
    // a member name it never wrote and ok == true.
    struct Member {
        std::string archive_name;
        fs::path source_path;
    };
    std::vector<Member> written_members;
    written_members.push_back({"manifest.json", fs::path("manifest.json")});

    // A member name already claimed by a file that is not equivalent to the
    // candidate. Resolving this by skipping the candidate is never correct:
    // two distinct files cannot share one archive slot, so refuse the package.
    const auto claim_member = [&](const std::string &archive_name, const fs::path &source_path,
                                  bool &already_written, bool &case_folded_collision,
                                  std::string &colliding_name) {
        already_written = false;
        case_folded_collision = false;
        for (const auto &member : written_members) {
            if (member.archive_name == archive_name) {
                std::error_code equivalent_ec;
                if (fs::equivalent(member.source_path, source_path, equivalent_ec) &&
                    !equivalent_ec)
                    already_written = true; // the same file, referenced again: one member
                return true;                // exact name: written or refused, never re-added
            }
            if (toLowerAscii(member.archive_name) == toLowerAscii(archive_name)) {
                case_folded_collision = true;
                colliding_name = member.archive_name;
                return true;
            }
        }
        written_members.push_back({archive_name, source_path});
        return false;
    };

    const auto collision_error = [](const std::string &claimed_name, const std::string &new_name) {
        return "case-insensitive package member collision between '" + claimed_name + "' and '" +
               new_name + "'";
    };

    for (const auto &entry : entries) {
        bool already_written = false;
        bool case_folded_collision = false;
        std::string colliding_name;
        if (claim_member(entry.archive_name, entry.source_path, already_written,
                         case_folded_collision, colliding_name)) {
            if (case_folded_collision)
                return fail(collision_error(colliding_name, entry.archive_name));
            if (already_written)
                continue;
            return fail("duplicate package member '" + entry.archive_name + "'");
        }

        if (mz_zip_writer_add_mem(&zip, entry.archive_name.c_str(), entry.content.data(),
                                  entry.content.size(), MZ_BEST_COMPRESSION) != MZ_TRUE) {
            return fail("cannot add '" + entry.archive_name + "' to the package: " + zipError(zip));
        }
        for (const auto &asset : entry.assets) {
            already_written = false;
            case_folded_collision = false;
            colliding_name.clear();
            const bool name_taken =
                claim_member(asset.archive_name, asset.source_path, already_written,
                             case_folded_collision, colliding_name);
            if (name_taken) {
                if (case_folded_collision)
                    return fail(collision_error(colliding_name, asset.archive_name));
                if (already_written)
                    continue; // the same file, referenced again: one member
                return fail("package members '" + asset.archive_name + "' from '" +
                            asset.source_path +
                            "' collide with a different file already written at that name");
            }

            if (mz_zip_writer_add_file(&zip, asset.archive_name.c_str(), asset.source_path.c_str(),
                                       nullptr, 0, MZ_BEST_COMPRESSION) != MZ_TRUE) {
                return fail("cannot add referenced data file '" + asset.raw_reference +
                            "' to the package: " + zipError(zip));
            }
        }
    }

    if (mz_zip_writer_finalize_archive(&zip) != MZ_TRUE)
        return fail("cannot finalize the package: " + zipError(zip));
    if (mz_zip_writer_end(&zip) != MZ_TRUE) {
        std::error_code ec;
        fs::remove(output_path, ec);
        result.error = "cannot close the package file: " + output_path.string();
        return result;
    }

    result.ok = true;
    result.output_path = output_path.string();
    result.definitions = static_cast<int>(entries.size());
    result.assets = asset_count;
    LOG_INFO("Exported %d component definition(s) and %d asset(s) to %s", result.definitions,
             result.assets, result.output_path.c_str());
    return result;
}
LibraryPackageImportResult importLibraryPackage(const std::filesystem::path &package_path,
                                                const std::filesystem::path &global_library_root) {
    LibraryPackageImportResult result;

    if (package_path.empty()) {
        result.error = "no package path given";
        return result;
    }
    if (global_library_root.empty()) {
        result.error = "no destination root given";
        return result;
    }

    // --- Open and inventory the archive -------------------------------------

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (mz_zip_reader_init_file(&zip, package_path.string().c_str(), 0) != MZ_TRUE) {
        result.error = "cannot open package file '" + package_path.string() + "': " + zipError(zip);
        return result;
    }
    // Every return below goes through this guard so the reader is always ended.
    struct ZipCloser {
        mz_zip_archive *zip;
        ~ZipCloser() { mz_zip_reader_end(zip); }
    } zip_closer{&zip};

    const mz_uint member_count = mz_zip_reader_get_num_files(&zip);
    if (member_count > library_package::kMaxArchiveMembers) {
        result.error = "archive has " + std::to_string(member_count) + " members (limit " +
                       std::to_string(library_package::kMaxArchiveMembers) + ")";
        return result;
    }

    struct MemberInfo {
        mz_uint index = 0;
        std::uint64_t size = 0;
    };
    std::map<std::string, MemberInfo> members_by_name; // exact archive spelling
    std::set<std::string> lowered_names;               // case-insensitive duplicates
    std::uint64_t declared_total = 0;

    for (mz_uint i = 0; i < member_count; ++i) {
        mz_zip_archive_file_stat stat;
        if (mz_zip_reader_file_stat(&zip, i, &stat) != MZ_TRUE) {
            result.error = "cannot read archive member " + std::to_string(i) + ": " + zipError(zip);
            return result;
        }
        const std::string name(stat.m_filename);

        if (!isSafeMemberPath(name)) {
            result.error = "unsafe package member path '" + name + "'";
            return result;
        }
        if (mz_zip_reader_is_file_a_directory(&zip, i) == MZ_TRUE) {
            result.error = "archive member '" + name + "' is a directory entry";
            return result;
        }
        if (stat.m_uncomp_size > library_package::kMaxMemberBytes) {
            result.error = "archive member '" + name + "' exceeds the per-member size limit";
            return result;
        }
        declared_total += stat.m_uncomp_size;
        if (declared_total > library_package::kMaxExpandedBytes) {
            result.error = "archive expands beyond the package size limit";
            return result;
        }
        if (!lowered_names.insert(toLowerAscii(name)).second) {
            result.error = "duplicate package member '" + name + "'";
            return result;
        }
        members_by_name.emplace(name, MemberInfo{i, stat.m_uncomp_size});
    }

    // --- Manifest -----------------------------------------------------------

    const auto manifest_it = members_by_name.find("manifest.json");
    if (manifest_it == members_by_name.end()) {
        result.error = "package has no manifest.json";
        return result;
    }
    std::string manifest_text(static_cast<std::size_t>(manifest_it->second.size), '\0');
    if (!manifest_text.empty() &&
        mz_zip_reader_extract_to_mem(&zip, manifest_it->second.index, manifest_text.data(),
                                     manifest_text.size(), 0) != MZ_TRUE) {
        result.error = "cannot read the package manifest: " + zipError(zip);
        return result;
    }

    if (manifest_text.empty()) {
        result.error = "package manifest is empty";
        return result;
    }

    json manifest;
    try {
        manifest = json::parse(manifest_text);
    } catch (const json::exception &e) {
        result.error = std::string("package manifest is not valid JSON: ") + e.what();
        return result;
    }

    if (!manifest.is_object()) {
        result.error = "package manifest is not a JSON object";
        return result;
    }
    if (!manifest.contains("format") || !manifest["format"].is_string() ||
        manifest["format"].get<std::string>() != library_package::kFormatId) {
        result.error = "not an RF Simulator library package";
        return result;
    }
    if (!manifest.contains("format_version") || !manifest["format_version"].is_number_integer() ||
        manifest["format_version"].get<long long>() != library_package::kFormatVersion) {
        result.error = "unsupported package format_version (expected " +
                       std::to_string(library_package::kFormatVersion) + ")";
        return result;
    }

    if (!manifest.contains("package_name") || !manifest["package_name"].is_string()) {
        result.error = "package_name is missing or not a string";
        return result;
    }
    const std::string package_name = manifest["package_name"].get<std::string>();
    if (package_name.empty() || sanitizePathSegment(package_name, "") != package_name) {
        result.error = "invalid package_name '" + package_name + "' (must be a bare path segment)";
        return result;
    }

    if (!manifest.contains("components") || !manifest["components"].is_array() ||
        manifest["components"].empty()) {
        result.error = "package has no components";
        return result;
    }

    std::vector<ManifestComponent> components;
    std::set<std::string> declared_identities;
    std::set<std::string> declared_json_members;
    for (const auto &entry : manifest["components"]) {
        if (!entry.is_object() || !entry.contains("json") || !entry["json"].is_string() ||
            !entry.contains("type") || !entry["type"].is_string() ||
            !entry.contains("manufacturer") || !entry["manufacturer"].is_string() ||
            !entry.contains("part_number") || !entry["part_number"].is_string() ||
            !entry.contains("assets") || !entry["assets"].is_array()) {
            result.error = "malformed manifest component entry";
            return result;
        }

        ManifestComponent component;
        component.json_member = entry["json"].get<std::string>();
        component.type = entry["type"].get<std::string>();
        component.manufacturer = entry["manufacturer"].get<std::string>();
        component.part_number = entry["part_number"].get<std::string>();
        for (const auto &asset : entry["assets"]) {
            if (!asset.is_string()) {
                result.error = "malformed asset entry for '" + component.part_number + "'";
                return result;
            }
            component.assets.push_back(asset.get<std::string>());
        }

        const std::string identity =
            identityKey(component.type, component.manufacturer, component.part_number);
        if (!declared_identities.insert(identity).second) {
            result.error = "duplicate component identity '" + component.type + "/" +
                           component.manufacturer + "/" + component.part_number + "' in manifest";
            return result;
        }
        if (!declared_json_members.insert(toLowerAscii(component.json_member)).second) {
            result.error =
                "duplicate component JSON reference '" + component.json_member + "' in manifest";
            return result;
        }
        components.push_back(std::move(component));
    }

    // Every manifest-listed member must be a safe library/ payload member that
    // the archive actually carries. Collect the extraction list in manifest
    // order, deduplicated (an asset shared by two definitions is one member).
    std::vector<std::string> payload_members;
    std::set<std::string> seen_payload;
    const auto require_payload = [&](const std::string &member,
                                     const std::string &part_number) -> bool {
        if (!payloadRelative(member)) {
            result.error = "packaged member '" + member + "' for '" + part_number +
                           "' is not a library/ payload member";
            return false;
        }
        if (members_by_name.find(member) == members_by_name.end()) {
            result.error = "packaged member '" + member + "' for '" + part_number +
                           "' is missing from the archive";
            return false;
        }
        if (seen_payload.insert(member).second)
            payload_members.push_back(member);
        return true;
    };

    for (const auto &component : components) {
        if (!require_payload(component.json_member, component.part_number))
            return result;
        for (const auto &asset : component.assets) {
            if (!require_payload(asset, component.part_number))
                return result;
        }
    }

    // --- Destination and conflicts ------------------------------------------

    const fs::path final_dir = global_library_root / package_name;
    std::error_code exists_ec;
    if (fs::exists(final_dir, exists_ec)) {
        result.error = "package directory already exists: " + final_dir.string();
        return result;
    }
    // Inventory existing definitions before creating staging; conflicts are
    // not reported until all package structure and definitions validate.
    ComponentLibrary existing;
    existing.scan(global_library_root.string());
    std::set<std::string> installed_identities;
    for (const auto *def : existing.all())
        installed_identities.insert(identityKey(def->type, def->manufacturer, def->part_number));

    // --- Stage the whole payload, then validate it as the loader sees it ----

    std::error_code root_ec;
    fs::create_directories(global_library_root, root_ec);
    if (root_ec && !fs::is_directory(global_library_root)) {
        result.error = "cannot create destination root '" + global_library_root.string() +
                       "': " + root_ec.message();
        return result;
    }

    fs::path staging;
    {
        const std::string base = ".import-" + package_name + "-" + std::to_string(processId());
        for (int attempt = 0; attempt < 1000 && staging.empty(); ++attempt) {
            const fs::path candidate = global_library_root / (base + "-" + std::to_string(attempt));
            if (!fs::exists(candidate))
                staging = candidate;
        }
        if (staging.empty()) {
            result.error =
                "cannot reserve a staging directory under '" + global_library_root.string() + "'";
            return result;
        }
    }
    // Removes the staging tree on every return from here on (rename clears it).
    StagingGuard staging_guard{staging};

    std::error_code staging_ec;
    fs::create_directories(staging, staging_ec);
    if (staging_ec) {
        result.error =
            "cannot create staging directory '" + staging.string() + "': " + staging_ec.message();
        return result;
    }

    std::uint64_t extracted_total = 0;
    for (const auto &member : payload_members) {
        const auto info = members_by_name.find(member);
        const fs::path relative = *payloadRelative(member);
        const fs::path destination = staging / relative;

        extracted_total += info->second.size;
        if (extracted_total > library_package::kMaxExpandedBytes) {
            result.error =
                "package expands beyond the size limit while extracting '" + member + "'";
            return result;
        }

        std::error_code dir_ec;
        fs::create_directories(destination.parent_path(), dir_ec);
        if (dir_ec) {
            result.error = "cannot create a directory for package member '" + member +
                           "': " + dir_ec.message();
            return result;
        }
        if (mz_zip_reader_extract_to_file(&zip, info->second.index, destination.string().c_str(),
                                          0) != MZ_TRUE) {
            result.error = "cannot extract package member '" + member + "': " + zipError(zip);
            return result;
        }
    }

    ComponentLibrary staged;
    staged.scan(staging.string());
    const auto staged_definitions = staged.all();

    // Each manifest JSON path must map to exactly one loader-accepted file.
    // This path-indexed map prevents set equality from allowing identities to
    // be silently exchanged between definitions.
    std::map<std::string, const ComponentDefinition *> definitions_by_path;
    std::error_code staging_canonical_ec;
    const fs::path canonical_staging = fs::weakly_canonical(staging, staging_canonical_ec);
    if (staging_canonical_ec) {
        result.error = "cannot resolve the staged package directory";
        return result;
    }
    for (const auto *def : staged_definitions) {
        std::error_code definition_ec;
        const fs::path definition_path = fs::weakly_canonical(def->source_path, definition_ec);
        if (definition_ec || !pathWithinRoot(canonical_staging, definition_path)) {
            result.error = "package payload validation failed: definition path is outside staging";
            return result;
        }
        const fs::path relative_path = definition_path.lexically_relative(canonical_staging);
        if (relative_path.empty() || relative_path.is_absolute() ||
            containsParentTraversal(relative_path) ||
            !definitions_by_path.emplace(toSlash(relative_path), def).second) {
            result.error =
                "package payload validation failed: multiple definitions map to one JSON path";
            return result;
        }
    }
    for (const auto &component : components) {
        const std::string expected = toSlash(*payloadRelative(component.json_member));
        if (!definitions_by_path.count(expected)) {
            result.error = "package payload validation failed: JSON path '" +
                           component.json_member + "' did not load";
            return result;
        }
    }
    if (definitions_by_path.size() != components.size()) {
        result.error =
            "package payload validation failed: multiple definitions map to manifest JSON paths";
        return result;
    }

    const std::set<std::string> extracted_members(payload_members.begin(), payload_members.end());
    for (const auto &component : components) {
        const fs::path relative_json = *payloadRelative(component.json_member);
        const std::string relative_json_key = toSlash(relative_json);
        const auto definition_it = definitions_by_path.find(relative_json_key);
        if (definition_it == definitions_by_path.end()) {
            result.error = "package payload validation failed: JSON path '" +
                           component.json_member + "' did not load";
            return result;
        }
        const ComponentDefinition &definition = *definition_it->second;
        if (identityKey(definition.type, definition.manufacturer, definition.part_number) !=
            identityKey(component.type, component.manufacturer, component.part_number)) {
            result.error = "package payload identity mismatch for '" + component.json_member +
                           "': manifest declares '" + component.type + "/" +
                           component.manufacturer + "/" + component.part_number +
                           "', JSON defines '" + definition.type + "/" + definition.manufacturer +
                           "/" + definition.part_number + "'";
            return result;
        }

        const fs::path json_path = staging / relative_json;
        const fs::path json_dir = json_path.parent_path();
        json definition_json;
        try {
            std::ifstream ifs(json_path);
            if (!ifs.is_open())
                throw std::runtime_error("cannot open staged JSON");
            ifs >> definition_json;
        } catch (const std::exception &e) {
            result.error = "cannot inspect JSON asset references in '" + component.json_member +
                           "': " + e.what();
            return result;
        }

        std::set<std::string> actual_asset_ids;
        const auto add_actual_reference = [&](const std::string &reference) {
            const auto resolved = resolveContained(json_dir, reference);
            std::error_code asset_ec;
            if (!resolved || !fs::is_regular_file(*resolved, asset_ec) || asset_ec ||
                !pathWithinRoot(canonical_staging, *resolved)) {
                result.error = "actual JSON asset reference '" + reference + "' for '" +
                               component.part_number +
                               "' is missing or outside its definition directory";
                return false;
            }
            std::error_code relative_ec;
            const fs::path relative_asset = fs::relative(*resolved, canonical_staging, relative_ec);
            if (relative_ec || relative_asset.empty() || relative_asset.is_absolute() ||
                containsParentTraversal(relative_asset)) {
                result.error = "cannot resolve actual JSON asset reference '" + reference + "'";
                return false;
            }
            const std::string expected_member = "library/" + toSlash(relative_asset);
            if (members_by_name.find(expected_member) == members_by_name.end() ||
                !extracted_members.count(expected_member)) {
                result.error = "actual JSON asset '" + expected_member + "' for '" +
                               component.part_number +
                               "' is not listed in the manifest and extracted";
                return false;
            }
            actual_asset_ids.insert(resolved->generic_string());
            return true;
        };

        if (definition_json.contains("data_files")) {
            if (!definition_json["data_files"].is_array()) {
                result.error = "malformed data_files references in '" + component.json_member + "'";
                return result;
            }
            for (const auto &data_file : definition_json["data_files"]) {
                if (!data_file.is_object() || !data_file.contains("type") ||
                    !data_file["type"].is_string() || !data_file.contains("path") ||
                    !data_file["path"].is_string()) {
                    result.error = "malformed data_files entry in '" + component.json_member + "'";
                    return result;
                }
                if (!add_actual_reference(data_file["path"].get<std::string>()))
                    return result;
            }
        }
        if (definition_json.contains("parameters")) {
            if (!definition_json["parameters"].is_object()) {
                result.error = "malformed parameters in '" + component.json_member + "'";
                return result;
            }
            for (const char *key : referenceParameterKeys()) {
                if (!definition_json["parameters"].contains(key))
                    continue;
                if (!definition_json["parameters"][key].is_string()) {
                    result.error = "malformed " + std::string(key) + " reference in '" +
                                   component.json_member + "'";
                    return result;
                }
                if (!add_actual_reference(definition_json["parameters"][key].get<std::string>()))
                    return result;
            }
        }

        std::set<std::string> listed_asset_ids;
        for (const auto &asset : component.assets) {
            const fs::path asset_path = staging / *payloadRelative(asset);
            std::error_code asset_ec;
            if (!pathWithinRoot(json_dir, asset_path) ||
                !fs::is_regular_file(asset_path, asset_ec) || asset_ec) {
                result.error = "packaged asset '" + asset + "' for '" + component.part_number +
                               "' is outside its definition directory (containment violation)";
                return result;
            }
            std::error_code canonical_asset_ec;
            const fs::path canonical_asset = fs::canonical(asset_path, canonical_asset_ec);
            if (canonical_asset_ec) {
                result.error = "cannot resolve packaged asset '" + asset + "'";
                return result;
            }
            listed_asset_ids.insert(canonical_asset.generic_string());
        }
        if (listed_asset_ids != actual_asset_ids) {
            result.error = "manifest asset list does not match actual JSON asset references for '" +
                           component.json_member + "'";
            return result;
        }
    }

    // Report and act on pre-existing identities only after validation completes.

    std::vector<std::size_t> accepted;
    for (std::size_t i = 0; i < components.size(); ++i) {
        const auto &component = components[i];
        const std::string identity =
            identityKey(component.type, component.manufacturer, component.part_number);
        if (installed_identities.count(identity)) {
            result.conflicts.push_back(component.type + " " + component.manufacturer + " " +
                                       component.part_number + ": already installed");
            continue;
        }
        accepted.push_back(i);
    }
    if (accepted.empty()) {
        // All good, but every identity already exists: install nothing.
        result.ok = true;
        LOG_INFO("Imported nothing from %s: all %zu component(s) already installed",
                 package_path.string().c_str(), components.size());
        return result;
    }

    // --- Install the accepted subset ---------------------------------------

    // Staging mirrored the whole validated payload; drop the conflicting
    // components so the renamed package contains only what was accepted. An
    // asset shared with an accepted component stays.
    std::set<std::size_t> accepted_set(accepted.begin(), accepted.end());
    std::set<std::string> accepted_assets;
    for (std::size_t i : accepted) {
        for (const auto &asset : components[i].assets)
            accepted_assets.insert(asset);
    }
    for (std::size_t i = 0; i < components.size(); ++i) {
        if (accepted_set.count(i))
            continue;
        const auto &component = components[i];

        // A removal that fails leaves a stale conflicting file behind and the
        // rename below would install it as if accepted, so refuse. `remove()`
        // only sets the error code when it actually failed (a file already
        // gone by a shared-asset removal reports no error).
        std::error_code remove_ec;
        const fs::path json_path = staging / *payloadRelative(component.json_member);
        fs::remove(json_path, remove_ec);
        if (remove_ec) {
            result.error = "cannot remove conflicting package file '" + json_path.string() +
                           "': " + remove_ec.message();
            return result; // staging_guard removes the staging tree
        }
        for (const auto &asset : component.assets) {
            if (accepted_assets.count(asset))
                continue;
            const fs::path asset_path = staging / *payloadRelative(asset);
            std::error_code asset_ec;
            fs::remove(asset_path, asset_ec);
            if (asset_ec) {
                result.error = "cannot remove conflicting package file '" + asset_path.string() +
                               "': " + asset_ec.message();
                return result;
            }
        }
    }

    // Drop directories emptied by those removals (deepest first), so the
    // installed package has no stale branches from skipped components. This is
    // best-effort: `remove()` refuses non-empty directories, so an accepted
    // entry can never be deleted here, and a leftover empty branch is harmless.
    {
        std::vector<fs::path> directories;
        std::error_code walk_ec;
        fs::recursive_directory_iterator it(staging, fs::directory_options::skip_permission_denied,
                                            walk_ec);
        const fs::recursive_directory_iterator end;
        for (; !walk_ec && it != end; it.increment(walk_ec)) {
            std::error_code type_ec;
            if (it->is_directory(type_ec) && !type_ec)
                directories.push_back(it->path());
        }
        for (auto dir_it = directories.rbegin(); dir_it != directories.rend(); ++dir_it) {
            std::error_code remove_ec;
            fs::remove(*dir_it, remove_ec); // non-empty directories are left in place
            if (remove_ec)
                LOG_WARN("Import: cannot prune emptied staging directory %s: %s",
                         dir_it->string().c_str(), remove_ec.message().c_str());
        }
    }

    std::error_code rename_ec;
    fs::rename(staging, final_dir, rename_ec);
    if (rename_ec) {
        result.error =
            "cannot install package directory '" + final_dir.string() + "': " + rename_ec.message();
        return result; // staging_guard removes the staging tree
    }
    staging_guard.path.clear(); // the tree now lives at final_dir

    result.ok = true;
    result.imported = static_cast<int>(accepted.size());
    result.installed_dir = final_dir.string();
    LOG_INFO("Imported %d component definition(s) from %s into %s", result.imported,
             package_path.string().c_str(), result.installed_dir.c_str());
    return result;
}
