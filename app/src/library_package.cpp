#include "library_package.h"

#include "component_library.h"
#include "logging_core.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <vector>

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

// `target` under `base`, both canonical; used to turn an absolute reference
// into a path relative to its own definition directory.
std::string relativeTo(const fs::path &target, const fs::path &base) {
    std::error_code ec;
    const fs::path rel = fs::relative(target, base, ec);
    if (ec)
        return toSlash(target);
    return toSlash(rel);
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
    const fs::path canonical_json = fs::weakly_canonical(json_path, ec);
    if (ec) {
        error = "cannot read library file: " + json_path.string();
        return false;
    }
    const fs::path json_dir = canonical_json.parent_path();

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

    std::error_code relative_ec;
    const fs::path relative_json = fs::relative(canonical_json, source_root, relative_ec);
    if (relative_ec) {
        error = "cannot derive a package path for: " + json_path.string();
        return false;
    }
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

    std::set<std::string> seen_references;

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
        // Case-insensitive dedupe: two references to the same file (or two
        // spellings differing only in case) become one archive member.
        const std::string key = toLowerAscii(resolved->generic_string());
        if (!seen_references.insert(key).second)
            return true;

        std::error_code rel_ec;
        const fs::path relative_asset = fs::relative(*resolved, source_root, rel_ec);
        if (rel_ec) {
            error = "referenced data file '" + raw_reference + "' for '" + def.part_number +
                    "' is outside the library root: " + json_path.string();
            return false;
        }

        ExportEntry::Asset asset;
        asset.archive_name = "library/" + toSlash(relative_asset);
        asset.source_path = resolved->string();
        asset.raw_reference = raw_reference;
        entry.assets.push_back(std::move(asset));
        return true;
    };

    // data_files: rewrite the in-memory copy when the spelling is an absolute
    // path inside the definition directory; relative spellings are preserved.
    if (raw.contains("data_files") && raw["data_files"].is_array()) {
        for (auto &df : raw["data_files"]) {
            if (!df.is_object() || !df.contains("type") || !df["type"].is_string() ||
                !df.contains("path") || !df["path"].is_string()) {
                // loadFile() isolates these, but the packaged copy must not
                // carry a reference export cannot account for.
                error = "malformed data_files entry in " + json_path.string();
                return false;
            }
            const std::string raw_reference = df["path"].get<std::string>();
            if (!addAsset(raw_reference))
                return false;

            const fs::path authored(raw_reference);
            if (authored.is_absolute()) {
                const auto resolved = resolveContained(json_dir, raw_reference);
                df["path"] = relativeTo(*resolved, json_dir);
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
            if (fs::path(raw_reference).is_absolute())
                raw["parameters"][key] =
                    relativeTo(*resolveContained(json_dir, raw_reference), json_dir);
        }
    }

    entry.content = raw.dump(2);
    return true;
}

} // namespace

LibraryPackageExportResult exportLibraryPackage(const std::filesystem::path &source_root,
                                                const std::filesystem::path &output_path) {
    LibraryPackageExportResult result;

    if (output_path.empty()) {
        result.error = "no output path given";
        return result;
    }

    std::vector<fs::path> json_files;
    if (!walkRoot(source_root, json_files, result.error))
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
        if (!collectEntry(source_root, json_path, entry, result.error))
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
    std::set<std::string> asset_members; // case-insensitive member names
    for (const auto &entry : entries) {
        json component = {{"json", entry.archive_name},
                          {"type", entry.type},
                          {"manufacturer", entry.manufacturer},
                          {"part_number", entry.part_number},
                          {"assets", json::array()}};
        for (const auto &asset : entry.assets) {
            component["assets"].push_back(asset.archive_name);
            if (asset_members.insert(toLowerAscii(asset.archive_name)).second)
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

    // Lowercased member name -> lowercased source path. Re-claiming a member
    // from the same source is the intended dedupe; a different source claiming
    // it means two distinct files map to one case-insensitive archive name, so
    // the package would be ambiguous (and its importer would reject it).
    std::map<std::string, std::string> written_members;
    written_members.emplace("manifest.json", "manifest.json");

    for (const auto &entry : entries) {
        const std::string member_key = toLowerAscii(entry.archive_name);
        const std::string source_key = toLowerAscii(entry.source_path);
        const auto existing = written_members.find(member_key);
        if (existing != written_members.end() && existing->second != source_key)
            return fail("duplicate package member '" + entry.archive_name + "'");
        written_members[member_key] = source_key;

        if (mz_zip_writer_add_mem(&zip, entry.archive_name.c_str(), entry.content.data(),
                                  entry.content.size(), MZ_BEST_COMPRESSION) != MZ_TRUE) {
            return fail("cannot add '" + entry.archive_name + "' to the package: " + zipError(zip));
        }
        for (const auto &asset : entry.assets) {
            const std::string asset_key = toLowerAscii(asset.archive_name);
            const std::string asset_source = toLowerAscii(asset.source_path);
            const auto asset_existing = written_members.find(asset_key);
            if (asset_existing != written_members.end()) {
                if (asset_existing->second == asset_source)
                    continue; // the same file, referenced again: one member
                return fail("package members '" + asset.archive_name +
                            "' collide case-insensitively with another entry");
            }
            written_members[asset_key] = asset_source;

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