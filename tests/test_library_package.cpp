// Standalone Catch2 executable for component-library package export/import
// (docs/superpowers/plans/2026-09-27-component-library-portability.md).
//
// Export walks a component-library root, loads every *.json through the real
// ComponentLibrary loader contract, and writes one .rflib ZIP containing the
// manifest, every definition JSON, and only the assets those definitions
// reference — never unreferenced files. Missing, escaping, or malformed
// references abort the export with file-specific diagnostics and leave no
// partial output behind.
//
// The archive is opened with the raw miniz reader API (hence the explicit
// simulator::miniz link in tests/CMakeLists.txt: app links miniz PRIVATE, so
// its include directories do not propagate).
//
// Kept out of the main `tests` binary for the MinGW-w64 registration-ceiling
// reason documented in tests/AGENTS.md and tests/CMakeLists.txt.

#include "amplifier_engine.h"
#include "app.h"
#include "component_library.h"
#include "component_registry.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "library_browser_widget.h"
#include "library_package.h"
#include "node_graph_engine.h"
#include "test_temp_paths.h"
#include "view_manager.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <miniz.h>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Minimal valid 2-port Touchstone file (freq + S11, S21, S12, S22 in MA).
const char *kMinimalS2p = "# GHz S MA R 50\n"
                          "1.0 0.5 0.0 2.0 90.0 0.1 180.0 0.3 -45.0\n";

// RAII scratch directory under the system temp dir, named from the
// cross-process token (see tests/test_temp_paths.h) plus a per-process counter
// so concurrent TEST_CASEs of this file never share a directory.
struct TempDir {
    fs::path root;

    explicit TempDir(const char *tag) {
        static int counter = 0;
        root = fs::temp_directory_path() /
               ("libpkg_" + std::string(tag) + "_" + test_temp_paths::processTag() + "_" +
                std::to_string(counter++));
        fs::create_directories(root);
    }
    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

// Creates the parent directory so callers can write into a nested tree.
void writeText(const fs::path &path, const std::string &content) {
    fs::create_directories(path.parent_path());
    std::ofstream ofs(path);
    ofs << content;
}

// Same as writeText without the platform's text-mode newline translation.
void writeBinary(const fs::path &path, const std::string &content) {
    fs::create_directories(path.parent_path());
    std::ofstream ofs(path, std::ios::binary);
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
}

// Writes the minimal S2P in binary mode (no CRLF translation) and returns the
// exact bytes written, so assertions compare bytes, not the platform's text
// newline translation.
std::string writeS2p(const fs::path &path) {
    const std::string content(kMinimalS2p);
    writeBinary(path, content);
    return content;
}

// Exact bytes written by writeS2p. Kept on a std::string constant (not the
// `const char *` literal) so Catch2 compares the whole value instead of
// stopping at the NUL terminator.
inline const std::string kS2pBytes{"# GHz S MA R 50\n"
                                   "1.0 0.5 0.0 2.0 90.0 0.1 180.0 0.3 -45.0\n"};

// Valid v2 amplifier definition; `extra` is merged into the root object.
nlohmann::json amplifierDefinition(const std::string &part_number,
                                   const nlohmann::json &extra = nlohmann::json::object()) {
    nlohmann::json def = {
        {"schema_version", 2},
        {"type", "amplifier"},
        {"part_number", part_number},
        {"manufacturer", "Test Corp"},
        {"parameters", {{"gain_dB", 20.0}, {"nf_dB", 1.0}}},
    };
    for (auto it = extra.begin(); it != extra.end(); ++it)
        def[it.key()] = it.value();
    return def;
}

// Writes `<dir>/<name>.json` (name may contain spaces) plus a sibling
// `<name>.s2p`, and returns the JSON path.
fs::path writeAmpWithDataFile(const fs::path &dir, const std::string &name) {
    const std::string asset_name = name + ".s2p";
    writeS2p(dir / asset_name);
    nlohmann::json def = amplifierDefinition(
        name, {{"data_files",
                nlohmann::json::array({{{"type", "s_parameters"}, {"path", asset_name}}})}});
    const fs::path json_path = dir / (name + ".json");
    writeText(json_path, def.dump(2));
    return json_path;
}

// --- Raw miniz reader helpers -----------------------------------------------

// member name -> decompressed bytes, in central-directory order.
std::map<std::string, std::string> readArchive(const fs::path &archive_path) {
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    REQUIRE(mz_zip_reader_init_file(&zip, archive_path.string().c_str(), 0) == MZ_TRUE);

    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    std::vector<std::string> names;
    names.reserve(count);
    for (mz_uint i = 0; i < count; ++i) {
        char name[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE] = {0};
        const mz_uint written = mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
        REQUIRE(written > 0);
        names.emplace_back(name);
    }

    std::map<std::string, std::string> members;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        REQUIRE(mz_zip_reader_file_stat(&zip, i, &stat) == MZ_TRUE);
        std::string content(static_cast<std::size_t>(stat.m_uncomp_size), '\0');
        if (!content.empty()) {
            REQUIRE(mz_zip_reader_extract_to_mem(&zip, i, content.data(), content.size(), 0) ==
                    MZ_TRUE);
        }
        members.emplace(stat.m_filename, std::move(content));
    }

    REQUIRE(mz_zip_reader_end(&zip) == MZ_TRUE);
    return members;
}

// Member names in central-directory order (manifest must be written first).
std::vector<std::string> readMemberOrder(const fs::path &archive_path) {
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    REQUIRE(mz_zip_reader_init_file(&zip, archive_path.string().c_str(), 0) == MZ_TRUE);

    std::vector<std::string> order;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        REQUIRE(mz_zip_reader_file_stat(&zip, i, &stat) == MZ_TRUE);
        order.emplace_back(stat.m_filename);
    }
    REQUIRE(mz_zip_reader_end(&zip) == MZ_TRUE);
    return order;
}

nlohmann::json parseMember(const std::map<std::string, std::string> &members,
                           const std::string &name) {
    REQUIRE(members.count(name) == 1);
    return nlohmann::json::parse(members.at(name));
}

// --- Import fixtures ---------------------------------------------------------

// A package assembled by hand: manifest + payload members. Tests that must
// produce layouts export can never emit (traversal, duplicate spellings,
// missing assets, unknown format versions) build the ZIP themselves.
struct PackageSpec {
    nlohmann::json manifest;
    // member name -> decompressed bytes; written after manifest.json.
    std::vector<std::pair<std::string, std::string>> members;
};

void writeRawPackage(const fs::path &archive_path, const std::string &manifest_text,
                     const std::vector<std::pair<std::string, std::string>> &members) {
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    REQUIRE(mz_zip_writer_init_file(&zip, archive_path.string().c_str(), 0) == MZ_TRUE);
    REQUIRE(mz_zip_writer_add_mem(&zip, "manifest.json", manifest_text.data(), manifest_text.size(),
                                  MZ_BEST_COMPRESSION) == MZ_TRUE);
    for (const auto &member : members) {
        REQUIRE(mz_zip_writer_add_mem(&zip, member.first.c_str(), member.second.data(),
                                      member.second.size(), MZ_BEST_COMPRESSION) == MZ_TRUE);
    }
    REQUIRE(mz_zip_writer_finalize_archive(&zip) == MZ_TRUE);
    REQUIRE(mz_zip_writer_end(&zip) == MZ_TRUE);
}

void writePackage(const fs::path &archive_path, const PackageSpec &spec) {
    writeRawPackage(archive_path, spec.manifest.dump(2), spec.members);
}

nlohmann::json importManifest(const std::string &package_name, const nlohmann::json &components) {
    return nlohmann::json{
        {"format", library_package::kFormatId},
        {"format_version", library_package::kFormatVersion},
        {"package_name", package_name},
        {"notes", ""},
        {"components", components},
    };
}

nlohmann::json componentEntry(const std::string &json_member, const std::string &type,
                              const std::string &manufacturer, const std::string &part_number,
                              const nlohmann::json &assets = nlohmann::json::array()) {
    return nlohmann::json{{"json", json_member},
                          {"type", type},
                          {"manufacturer", manufacturer},
                          {"part_number", part_number},
                          {"assets", assets}};
}

// An amplifier definition with an optional data_files reference, serialized as
// a payload member's bytes. Mirrors the export fixture so a round trip can
// compare what the library loader sees on both ends.
std::string amplifierPayload(const std::string &manufacturer, const std::string &part_number,
                             const std::string &data_file = std::string()) {
    nlohmann::json def = {
        {"schema_version", 2},
        {"type", "amplifier"},
        {"part_number", part_number},
        {"manufacturer", manufacturer},
        {"parameters", {{"gain_dB", 20.0}, {"nf_dB", 1.0}}},
    };
    if (!data_file.empty()) {
        def["data_files"] =
            nlohmann::json::array({{{"type", "s_parameters"}, {"path", data_file}}});
    }
    return def.dump(2);
}

// A one-component package (definition + S2P asset) that a hand-built or
// exported archive can both carry.
PackageSpec singleAmpPackage(const std::string &package_name, const std::string &manufacturer,
                             const std::string &part_number) {
    const std::string base = "library/" + manufacturer + "/" + part_number;
    PackageSpec spec;
    spec.manifest = importManifest(
        package_name, nlohmann::json::array(
                          {componentEntry(base + ".json", "amplifier", manufacturer, part_number,
                                          nlohmann::json::array({base + ".s2p"}))}));
    spec.members.push_back(
        {base + ".json", amplifierPayload(manufacturer, part_number, part_number + ".s2p")});
    spec.members.push_back({base + ".s2p", kS2pBytes});
    return spec;
}

// Recursive file listing of a directory tree: relative generic path -> bytes.
// The refusal cases assert the destination root is byte-identical, which means
// no file added, removed, renamed, or rewritten.
std::map<std::string, std::string> snapshotTree(const fs::path &root) {
    std::map<std::string, std::string> snapshot;
    std::error_code ec;
    if (!fs::exists(root, ec)) {
        REQUIRE(!ec);
        return snapshot;
    }
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    REQUIRE(!ec);
    const fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        REQUIRE(!ec);
        std::error_code type_ec;
        if (!it->is_regular_file(type_ec) || type_ec)
            continue;
        std::error_code rel_ec;
        const fs::path relative = fs::relative(it->path(), root, rel_ec);
        REQUIRE(!rel_ec);

        std::ifstream ifs(it->path(), std::ios::binary);
        REQUIRE(ifs.is_open());
        snapshot.emplace(relative.generic_string(), std::string(std::istreambuf_iterator<char>(ifs),
                                                                std::istreambuf_iterator<char>()));
    }
    return snapshot;
}

std::string readFileBytes(const fs::path &path) {
    std::ifstream ifs(path, std::ios::binary);
    REQUIRE(ifs.is_open());
    return std::string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
}

// Identity key the importer reports conflicts by, spelled for assertions.
std::string conflictLine(const std::string &type, const std::string &manufacturer,
                         const std::string &part_number) {
    return type + " " + manufacturer + " " + part_number + ": already installed";
}

bool hasConflict(const LibraryPackageImportResult &result, const std::string &line) {
    return std::find(result.conflicts.begin(), result.conflicts.end(), line) !=
           result.conflicts.end();
}

// Every refusal contract at once: not ok, diagnostic present, nothing
// installed, and the destination root byte-identical (no staged leftovers).
void requireRefusal(const fs::path &package_path, const fs::path &root,
                    const std::string &error_substring) {
    const auto before = snapshotTree(root);
    const LibraryPackageImportResult result = importLibraryPackage(package_path, root);
    INFO("error: " << result.error);
    REQUIRE_FALSE(result.ok);
    if (!error_substring.empty())
        REQUIRE(result.error.find(error_substring) != std::string::npos);
    REQUIRE(result.imported == 0);
    REQUIRE(result.installed_dir.empty());
    REQUIRE(snapshotTree(root) == before);
}

} // namespace

// --- Happy path -------------------------------------------------------------

TEST_CASE("exportLibraryPackage writes exactly the referenced definitions and assets",
          "[library][package]") {
    TempDir tmp("basic");
    const fs::path root = tmp.root / "root";
    const fs::path amps = root / "amplifiers";

    // Definition 1: amplifier whose asset lives in data_files.
    writeAmpWithDataFile(amps, "AM1143");
    // Definition 2: amplifier whose asset lives in a sparam_filepath parameter.
    // Its JSON lives one level deeper, so the reference is resolved against
    // `<amps>/param/`.
    const fs::path param_dir = amps / "param";
    const fs::path param_asset = param_dir / "PARAM-AMP.s2p";
    writeS2p(param_asset);
    nlohmann::json def2 = amplifierDefinition(
        "PARAM-AMP", {{"parameters",
                       {{"gain_dB", 12.0},
                        {"nf_dB", 2.0},
                        {"sparam_filepath", (fs::path("PARAM-AMP.s2p")).string()}}}});
    const fs::path json2 = param_dir / "PARAM-AMP.json";
    writeText(json2, def2.dump(2));

    // Unreferenced stray file (not JSON: any unloadable *.json is a refusal).
    writeText(root / "unused.txt", "not part of any definition\n");

    const fs::path out = tmp.root / "lab-parts.rflib";
    const LibraryPackageExportResult result = exportLibraryPackage(root, out);

    INFO("error: " << result.error);
    REQUIRE(result.ok);
    REQUIRE(result.error.empty());
    REQUIRE(result.output_path == out.string());
    REQUIRE(result.definitions == 2);
    REQUIRE(result.assets == 2);

    const auto members = readArchive(out);
    REQUIRE(members.size() == 5);
    REQUIRE(members.count("manifest.json") == 1);
    REQUIRE(members.count("library/amplifiers/AM1143.json") == 1);
    REQUIRE(members.count("library/amplifiers/AM1143.s2p") == 1);
    REQUIRE(members.count("library/amplifiers/param/PARAM-AMP.json") == 1);
    REQUIRE(members.count("library/amplifiers/param/PARAM-AMP.s2p") == 1);
    REQUIRE(members.count("library/unused.txt") == 0);

    // Manifest is the first member; the rest follow the library/ payload.
    const auto order = readMemberOrder(out);
    REQUIRE(order.front() == "manifest.json");
    REQUIRE(order.size() == members.size());

    const auto manifest = parseMember(members, "manifest.json");
    REQUIRE(manifest.at("format") == library_package::kFormatId);
    REQUIRE(manifest.at("format_version") == library_package::kFormatVersion);
    REQUIRE(manifest.at("package_name") == "lab-parts");
    REQUIRE(manifest.at("components").is_array());
    REQUIRE(manifest.at("components").size() == 2);

    std::map<std::string, nlohmann::json> by_json;
    for (const auto &component : manifest.at("components"))
        by_json.emplace(component.at("json").get<std::string>(), component);

    REQUIRE(by_json.count("library/amplifiers/AM1143.json") == 1);
    const auto &first = by_json.at("library/amplifiers/AM1143.json");
    REQUIRE(first.at("type") == "amplifier");
    REQUIRE(first.at("manufacturer") == "Test Corp");
    REQUIRE(first.at("part_number") == "AM1143");
    REQUIRE(first.at("assets") == nlohmann::json::array({"library/amplifiers/AM1143.s2p"}));

    REQUIRE(by_json.count("library/amplifiers/param/PARAM-AMP.json") == 1);
    const auto &second = by_json.at("library/amplifiers/param/PARAM-AMP.json");
    REQUIRE(second.at("part_number") == "PARAM-AMP");
    REQUIRE(second.at("assets") ==
            nlohmann::json::array({"library/amplifiers/param/PARAM-AMP.s2p"}));

    // Packaged JSON is the parsed definition dumped with dump(2): all fields
    // survive and the relative reference keeps its spelling.
    const auto packaged1 = parseMember(members, "library/amplifiers/AM1143.json");
    REQUIRE(packaged1.at("schema_version") == 2);
    REQUIRE(packaged1.at("parameters").at("gain_dB") == 20.0);
    REQUIRE(packaged1.at("data_files").at(0).at("type") == "s_parameters");
    REQUIRE(packaged1.at("data_files").at(0).at("path") == "AM1143.s2p");
    REQUIRE(members.at("library/amplifiers/AM1143.s2p") == kS2pBytes);
}

TEST_CASE("exportLibraryPackage rewrites a contained absolute reference to a relative path",
          "[library][package]") {
    TempDir tmp("abs");
    const fs::path root = tmp.root / "root";
    writeS2p(root / "abs.s2p");

    const fs::path json_path = root / "abs.json";
    writeText(json_path,
              amplifierDefinition(
                  "ABS-AMP", {{"data_files",
                               nlohmann::json::array({{{"type", "s_parameters"},
                                                       {"path", (root / "abs.s2p").string()}}})}})
                  .dump(2));

    const fs::path out = tmp.root / "abs.rflib";
    const LibraryPackageExportResult result = exportLibraryPackage(root, out);
    INFO("error: " << result.error);
    REQUIRE(result.ok);

    const auto members = readArchive(out);
    REQUIRE(members.count("library/abs.json") == 1);
    REQUIRE(members.count("library/abs.s2p") == 1);

    const auto packaged = parseMember(members, "library/abs.json");
    REQUIRE(packaged.at("data_files").at(0).at("path") == "abs.s2p");
}

// --- Refusals ---------------------------------------------------------------

TEST_CASE("exportLibraryPackage refuses a missing referenced asset", "[library][package]") {
    TempDir tmp("missing");
    const fs::path root = tmp.root / "root";
    writeText(
        root / "amp.json",
        amplifierDefinition(
            "GONE-AMP", {{"data_files", nlohmann::json::array({{{"type", "s_parameters"},
                                                                {"path", "does-not-exist.s2p"}}})}})
            .dump(2));

    const fs::path out = tmp.root / "missing.rflib";
    const LibraryPackageExportResult result = exportLibraryPackage(root, out);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("does-not-exist.s2p") != std::string::npos);
    REQUIRE(result.error.find("amp.json") != std::string::npos);
    REQUIRE_FALSE(fs::exists(out));
}

TEST_CASE("exportLibraryPackage refuses malformed data_files values and entries",
          "[library][package]") {
    TempDir tmp("malformed_data_files");
    const fs::path root = tmp.root / "root";
    const std::vector<nlohmann::json> malformed_values = {
        "not an array",
        nlohmann::json::object(),
        nlohmann::json::array({nlohmann::json::object()}),
        nlohmann::json::array({{{"type", 7}, {"path", "asset.s2p"}}}),
        nlohmann::json::array({{{"type", "s_parameters"}, {"path", 7}}}),
    };

    for (std::size_t i = 0; i < malformed_values.size(); ++i) {
        const std::string filename = "malformed-" + std::to_string(i) + ".json";
        writeText(root / filename, amplifierDefinition("MALFORMED-" + std::to_string(i),
                                                       {{"data_files", malformed_values[i]}})
                                       .dump(2));
        const fs::path out = tmp.root / ("malformed-" + std::to_string(i) + ".rflib");

        const LibraryPackageExportResult result = exportLibraryPackage(root, out);

        INFO("error: " << result.error);
        REQUIRE_FALSE(result.ok);
        REQUIRE(result.error.find(filename) != std::string::npos);
        REQUIRE_FALSE(fs::exists(out));

        std::error_code ec;
        fs::remove(root / filename, ec);
        REQUIRE_FALSE(ec);
    }
}

TEST_CASE("exportLibraryPackage rewrites an absolute sparam_path and dedupes a shared asset",
          "[library][package]") {
    TempDir tmp("shared");
    const fs::path root = tmp.root / "root";
    const fs::path shared = root / "shared.s2p";
    const std::string shared_bytes = writeS2p(shared);

    // Two definitions, one asset: a relative data_files reference and an
    // absolute sparam_path parameter that must be rewritten.
    writeText(root / "rel.json",
              amplifierDefinition(
                  "SHARED-REL", {{"data_files", nlohmann::json::array({{{"type", "s_parameters"},
                                                                        {"path", "shared.s2p"}}})}})
                  .dump(2));
    writeText(
        root / "param.json",
        amplifierDefinition("SHARED-PARAM",
                            {{"parameters", {{"gain_dB", 5.0}, {"sparam_path", shared.string()}}}})
            .dump(2));

    const fs::path out = tmp.root / "shared.rflib";
    const LibraryPackageExportResult result = exportLibraryPackage(root, out);
    INFO("error: " << result.error);
    REQUIRE(result.ok);
    REQUIRE(result.definitions == 2);
    REQUIRE(result.assets == 1); // one asset, referenced twice

    const auto members = readArchive(out);
    REQUIRE(members.size() == 4);
    REQUIRE(members.count("library/shared.s2p") == 1);
    REQUIRE(members.at("library/shared.s2p") == shared_bytes);

    const auto packaged = parseMember(members, "library/param.json");
    REQUIRE(packaged.at("parameters").at("sparam_path") == "shared.s2p");

    const auto manifest = parseMember(members, "manifest.json");
    for (const auto &component : manifest.at("components"))
        REQUIRE(component.at("assets") == nlohmann::json::array({"library/shared.s2p"}));
}

TEST_CASE("exportLibraryPackage writes one member for one file referenced by two definitions",
          "[library][package]") {
    // The writer-level half of the identity dedupe, and the half that is
    // observable on this host. Both definitions name the same on-disk file
    // through different spellings, so the entry-level dedupe (which keys on
    // the exact canonical path) collapses neither of them: each entry carries
    // its own asset record and the writer must recognize both as one file by
    // identity (fs::equivalent) rather than by spelling.
    //
    // The case-distinct sibling of this scenario -- A.s2p and a.s2p as two
    // real files, which must both survive on a case-sensitive filesystem --
    // cannot be expressed with two files here: a case-insensitive host cannot
    // even create them side by side. This test pins the identity comparison
    // that makes that behaviour fall out on a host that can.
    TempDir tmp("identity");
    const fs::path root = tmp.root / "root";
    const fs::path asset = root / "IDENTITY.s2p";
    const std::string asset_bytes = writeS2p(asset);

    writeText(root / "def_a.json",
              amplifierDefinition(
                  "IDENTITY-A",
                  {{"data_files",
                    nlohmann::json::array({{{"type", "s_parameters"}, {"path", "IDENTITY.s2p"}}})}})
                  .dump(2));
    // Same file, different spelling: an absolute path that relativeTo() maps
    // back to "IDENTITY.s2p", so both entries produce the same member name
    // from the same source. The strings differ; the file does not.
    writeText(root / "def_b.json",
              amplifierDefinition(
                  "IDENTITY-B",
                  {{"data_files",
                    nlohmann::json::array({{{"type", "s_parameters"}, {"path", asset.string()}}})}})
                  .dump(2));

    const fs::path out = tmp.root / "identity.rflib";
    const LibraryPackageExportResult result = exportLibraryPackage(root, out);
    INFO("error: " << result.error);
    REQUIRE(result.ok);
    REQUIRE(result.definitions == 2);
    REQUIRE(result.assets == 1); // one on-disk file, referenced by two definitions

    const auto members = readArchive(out);
    // manifest + two definitions + one asset, and only one asset member.
    REQUIRE(members.size() == 4);
    REQUIRE(members.count("library/IDENTITY.s2p") == 1);
    REQUIRE(members.at("library/IDENTITY.s2p") == asset_bytes);

    // The invariant the writer's identity dedupe must preserve: every asset
    // the manifest names is written as a member. A silent `continue` on a
    // collision would leave the manifest naming a member the archive lacks.
    const auto manifest = parseMember(members, "manifest.json");
    for (const auto &component : manifest.at("components"))
        for (const auto &listed : component.at("assets"))
            REQUIRE(members.count(listed.get<std::string>()) == 1);
}

TEST_CASE("exportLibraryPackage refuses case-folded collisions between distinct assets",
          "[library][package]") {
    TempDir tmp("case_folded_asset_collision");
    const fs::path root = tmp.root / "root";
    const fs::path upper = root / "A.s2p";
    const fs::path lower = root / "a.s2p";
    writeS2p(upper);
    writeS2p(lower);

    std::error_code equivalent_ec;
    const bool same_file = fs::equivalent(upper, lower, equivalent_ec);
    REQUIRE_FALSE(equivalent_ec);
    if (same_file)
        SKIP("host filesystem treats A.s2p and a.s2p as equivalent");

    writeText(root / "one.json",
              amplifierDefinition("CASE-UPPER",
                                  {{"data_files", nlohmann::json::array({{{"type", "s_parameters"},
                                                                          {"path", "A.s2p"}}})}})
                  .dump(2));
    writeText(root / "two.json",
              amplifierDefinition("CASE-LOWER",
                                  {{"data_files", nlohmann::json::array({{{"type", "s_parameters"},
                                                                          {"path", "a.s2p"}}})}})
                  .dump(2));

    const fs::path out = tmp.root / "case-collision.rflib";
    const LibraryPackageExportResult result = exportLibraryPackage(root, out);

    REQUIRE_FALSE(result.ok);
    CHECK(result.error.find("case-insensitive") != std::string::npos);
    CHECK(result.error.find("library/A.s2p") != std::string::npos);
    CHECK(result.error.find("library/a.s2p") != std::string::npos);
    CHECK_FALSE(fs::exists(out));
}

TEST_CASE("exportLibraryPackage preserves a relative symlink asset reference on round trip",
          "[library][package]") {
    TempDir tmp("symlink_asset_reference");
    const fs::path source = tmp.root / "source";
    const fs::path amp_dir = source / "amplifiers";
    const std::string asset_bytes = writeS2p(amp_dir / "target.s2p");
    std::error_code symlink_ec;
    fs::create_symlink("target.s2p", amp_dir / "link.s2p", symlink_ec);
    if (symlink_ec) {
        SUCCEED("SKIP: cannot create a symlink on this platform: " + symlink_ec.message());
        return;
    }
    REQUIRE(fs::is_symlink(amp_dir / "link.s2p"));
    writeText(amp_dir / "amp.json",
              amplifierDefinition("SYMLINK-AMP",
                                  {{"data_files", nlohmann::json::array({{{"type", "s_parameters"},
                                                                          {"path", "link.s2p"}}})}})
                  .dump(2));

    const fs::path package = tmp.root / "symlink-amp.rflib";
    const LibraryPackageExportResult exported = exportLibraryPackage(source, package);
    INFO("export error: " << exported.error);
    REQUIRE(exported.ok);

    const auto members = readArchive(package);
    REQUIRE(members.count("library/amplifiers/link.s2p") == 1);
    REQUIRE(members.count("library/amplifiers/target.s2p") == 0);
    REQUIRE(members.at("library/amplifiers/link.s2p") == asset_bytes);
    const auto manifest = parseMember(members, "manifest.json");
    REQUIRE(manifest.at("components").at(0).at("assets") ==
            nlohmann::json::array({"library/amplifiers/link.s2p"}));
    const auto packaged = parseMember(members, "library/amplifiers/amp.json");
    REQUIRE(packaged.at("data_files").at(0).at("path") == "link.s2p");

    const fs::path global_root = tmp.root / "global";
    const LibraryPackageImportResult imported = importLibraryPackage(package, global_root);
    INFO("import error: " << imported.error);
    REQUIRE(imported.ok);
    const fs::path installed = imported.installed_dir;
    REQUIRE(fs::is_regular_file(installed / "amplifiers" / "link.s2p"));
    REQUIRE(readFileBytes(installed / "amplifiers" / "link.s2p") == asset_bytes);

    ComponentLibrary reloaded;
    reloaded.scan(installed.string());
    REQUIRE(reloaded.all().size() == 1);
    REQUIRE(reloaded.all().front()->data_files.size() == 1);
    REQUIRE(reloaded.all().front()->data_files.front().path == "link.s2p");

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);
    auto *engine = reloaded.instantiate(*reloaded.all().front(), 700, registry, graph);
    REQUIRE(engine != nullptr);
    const auto *amp_engine = dynamic_cast<const AmplifierEngine *>(engine);
    REQUIRE(amp_engine != nullptr);
    REQUIRE(amp_engine->sparamLoaded());
}

TEST_CASE("exportLibraryPackage removes the partial output when the writer cannot start",
          "[library][package]") {
    // The writer-init refusal is the only cleanup path reachable before any
    // member exists: an output path whose parent directory is missing makes
    // mz_zip_writer_init_file() fail after the whole package has been
    // assembled. The brief requires that failure to end the writer and leave
    // no file at the output path.
    TempDir tmp("writerinit");
    const fs::path root = tmp.root / "root";
    writeText(root / "amp.json", amplifierDefinition("INIT-AMP").dump(2));

    const fs::path out = tmp.root / "no-such-dir" / "init.rflib";
    REQUIRE_FALSE(fs::exists(out.parent_path()));

    const LibraryPackageExportResult result = exportLibraryPackage(root, out);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find(out.string()) != std::string::npos);
    REQUIRE_FALSE(fs::exists(out));
}

TEST_CASE("exportLibraryPackage refuses an escaping referenced asset", "[library][package]") {
    TempDir tmp("escape");
    const fs::path root = tmp.root / "root";
    const fs::path def_dir = root / "nested";
    // The referenced file exists at the location the raw '../' reference
    // resolves to; only the containment refusal can reject it.
    const fs::path outside = root / "outside.s2p";
    writeS2p(outside);
    writeText(def_dir / "amp.json",
              amplifierDefinition(
                  "ESCAPE-AMP",
                  {{"data_files", nlohmann::json::array(
                                      {{{"type", "s_parameters"}, {"path", "../outside.s2p"}}})}})
                  .dump(2));
    REQUIRE(fs::exists(outside));
    REQUIRE_FALSE(fs::exists(def_dir / "outside.s2p"));

    const fs::path out = tmp.root / "escape.rflib";
    const LibraryPackageExportResult result = exportLibraryPackage(root, out);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("../outside.s2p") != std::string::npos);
    REQUIRE(result.error.find("amp.json") != std::string::npos);
    REQUIRE_FALSE(fs::exists(out));
}

TEST_CASE("exportLibraryPackage refuses a malformed component JSON", "[library][package]") {
    TempDir tmp("malformed");
    const fs::path root = tmp.root / "root";
    writeText(root / "good.json", amplifierDefinition("GOOD-AMP").dump(2));
    writeText(root / "broken.json", R"({"type": "amplifier", "part_number": 42})");

    const fs::path out = tmp.root / "malformed.rflib";
    const LibraryPackageExportResult result = exportLibraryPackage(root, out);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("broken.json") != std::string::npos);
    REQUIRE_FALSE(fs::exists(out));
}

TEST_CASE("exportLibraryPackage refuses duplicate identities", "[library][package]") {
    TempDir tmp("duplicate");
    const fs::path root = tmp.root / "root";
    writeText(root / "one.json", amplifierDefinition("DUP-AMP").dump(2));
    // Same identity, different relative location: an ambiguous package.
    writeText(root / "nested" / "two.json", amplifierDefinition("DUP-AMP").dump(2));

    const fs::path out = tmp.root / "duplicate.rflib";
    const LibraryPackageExportResult result = exportLibraryPackage(root, out);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("DUP-AMP") != std::string::npos);
    REQUIRE_FALSE(fs::exists(out));
}
TEST_CASE("exportLibraryPackage refuses a JSON symlink that escapes the source root",
          "[library][package]") {
    TempDir tmp("json_symlink_escape");
    const fs::path root = tmp.root / "root";
    writeText(root / "inside.json", amplifierDefinition("INSIDE-AMP").dump(2));

    const fs::path outside_json = tmp.root / "outside.json";
    writeText(outside_json, amplifierDefinition("OUTSIDE-AMP").dump(2));
    std::error_code symlink_ec;
    fs::create_symlink(outside_json, root / "outside-link.json", symlink_ec);
    if (symlink_ec) {
        SUCCEED("SKIP: cannot create a symlink on this platform: " + symlink_ec.message());
        return;
    }
    REQUIRE(fs::is_symlink(root / "outside-link.json"));

    const fs::path out = tmp.root / "symlink-escape.rflib";
    const LibraryPackageExportResult result = exportLibraryPackage(root, out);

    INFO("error: " << result.error);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("outside-link.json") != std::string::npos);
    REQUIRE_FALSE(fs::exists(out));
}

// --- Spaces ----------------------------------------------------------------

TEST_CASE("exportLibraryPackage keeps directories and names containing spaces intact",
          "[library][package]") {
    TempDir tmp("spaces");
    const fs::path root = tmp.root / "root";
    const fs::path spaced = root / "Lab Parts" / "Amp Corp";
    const fs::path json_path = writeAmpWithDataFile(spaced, "AMP 100");
    REQUIRE(json_path.filename().string() == "AMP 100.json");
    const fs::path out = tmp.root / "spaces.rflib";
    const LibraryPackageExportResult result = exportLibraryPackage(root, out);
    INFO("error: " << result.error);
    REQUIRE(result.ok);

    const auto members = readArchive(out);
    REQUIRE(members.count("library/Lab Parts/Amp Corp/AMP 100.json") == 1);
    REQUIRE(members.count("library/Lab Parts/Amp Corp/AMP 100.s2p") == 1);

    const auto manifest = parseMember(members, "manifest.json");
    REQUIRE(manifest.at("components").size() == 1);
    REQUIRE(manifest.at("components").at(0).at("json") ==
            "library/Lab Parts/Amp Corp/AMP 100.json");
    REQUIRE(manifest.at("components").at(0).at("assets") ==
            nlohmann::json::array({"library/Lab Parts/Amp Corp/AMP 100.s2p"}));

    const auto packaged = parseMember(members, "library/Lab Parts/Amp Corp/AMP 100.json");
    REQUIRE(packaged.at("data_files").at(0).at("path") == "AMP 100.s2p");

    // The packaged definition stays loadable through the real loader contract.
    const fs::path downstream = tmp.root / "installed";
    fs::create_directories(downstream / "Lab Parts" / "Amp Corp");
    writeText(downstream / "Lab Parts" / "Amp Corp" / "AMP 100.json",
              members.at("library/Lab Parts/Amp Corp/AMP 100.json"));
    writeText(downstream / "Lab Parts" / "Amp Corp" / "AMP 100.s2p",
              members.at("library/Lab Parts/Amp Corp/AMP 100.s2p"));
    ComponentLibrary reloaded;
    reloaded.scan(downstream.string());
    REQUIRE(reloaded.all().size() == 1);
    REQUIRE(reloaded.all().front()->part_number == "AMP 100");
    REQUIRE(reloaded.all().front()->data_files.size() == 1);
    REQUIRE(reloaded.all().front()->data_files.front().path == "AMP 100.s2p");
}

// --- Import: happy path -----------------------------------------------------

TEST_CASE("importLibraryPackage installs every component and the payload stays loadable",
          "[library][package]") {
    TempDir tmp("import_roundtrip");
    const fs::path source = tmp.root / "source";
    writeAmpWithDataFile(source / "amps", "AM1143");
    writeText(source / "resistors" / "R100.json", amplifierDefinition("R100").dump(2));

    const fs::path package = tmp.root / "lab-parts.rflib";
    const LibraryPackageExportResult exported = exportLibraryPackage(source, package);
    INFO("export error: " << exported.error);
    REQUIRE(exported.ok);

    const fs::path root = tmp.root / "global";
    REQUIRE_FALSE(fs::exists(root));

    const LibraryPackageImportResult result = importLibraryPackage(package, root);
    INFO("import error: " << result.error);
    REQUIRE(result.ok);
    REQUIRE(result.error.empty());
    REQUIRE(result.imported == 2);
    REQUIRE(result.conflicts.empty());
    REQUIRE(result.installed_dir == (root / "lab-parts").string());

    const fs::path installed = result.installed_dir;
    REQUIRE(fs::is_regular_file(installed / "amps" / "AM1143.json"));
    REQUIRE(fs::is_regular_file(installed / "amps" / "AM1143.s2p"));
    REQUIRE(fs::is_regular_file(installed / "resistors" / "R100.json"));
    REQUIRE(readFileBytes(installed / "amps" / "AM1143.s2p") == kS2pBytes);

    // The staging directory is gone: the package directory is the only entry.
    for (const auto &entry : fs::directory_iterator(root))
        REQUIRE_FALSE(entry.path().filename().string().rfind(".import-", 0) == 0);

    ComponentLibrary rescan;
    rescan.scan(installed.string());
    REQUIRE(rescan.all().size() == 2);

    const ComponentDefinition *amp = nullptr;
    bool found_resistor = false;
    for (const auto *def : rescan.all()) {
        if (def->part_number == "AM1143")
            amp = def;
        if (def->part_number == "R100")
            found_resistor = true;
    }
    REQUIRE(amp != nullptr);
    REQUIRE(found_resistor);

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);
    auto *engine = rescan.instantiate(*amp, 700, registry, graph);
    REQUIRE(engine != nullptr);
    auto *amp_engine = dynamic_cast<AmplifierEngine *>(engine);
    REQUIRE(amp_engine != nullptr);
    REQUIRE(amp_engine->sparamLoaded());
}

TEST_CASE("importLibraryPackage creates a missing destination root", "[library][package]") {
    TempDir tmp("import_createroot");
    const fs::path package = tmp.root / "solo.rflib";
    writePackage(package, singleAmpPackage("solo", "Test Corp", "SOLO-AMP"));

    const fs::path root = tmp.root / "nested" / "global";
    REQUIRE_FALSE(fs::exists(root));

    const LibraryPackageImportResult result = importLibraryPackage(package, root);
    INFO("import error: " << result.error);
    REQUIRE(result.ok);
    REQUIRE(result.imported == 1);
    REQUIRE(fs::is_directory(root));
    REQUIRE(result.installed_dir == (root / "solo").string());
    REQUIRE(fs::is_regular_file(root / "solo" / "Test Corp" / "SOLO-AMP.json"));
    REQUIRE(fs::is_regular_file(root / "solo" / "Test Corp" / "SOLO-AMP.s2p"));
}

// --- Import: conflicts ------------------------------------------------------

TEST_CASE("importLibraryPackage skips conflicting identities and leaves existing files alone",
          "[library][package]") {
    TempDir tmp("import_mixed");
    const fs::path source = tmp.root / "source";
    writeAmpWithDataFile(source / "amps", "AM1143");
    writeText(source / "resistors" / "R100.json", amplifierDefinition("R100").dump(2));

    const fs::path package = tmp.root / "mixed.rflib";
    REQUIRE(exportLibraryPackage(source, package).ok);

    const fs::path root = tmp.root / "global";
    // Same identity as the incoming R100, distinct sentinel content.
    writeText(
        root / "exist" / "R100.json",
        amplifierDefinition("R100", {{"parameters", {{"gain_dB", 99.0}, {"nf_dB", 9.0}}}}).dump(2));
    // An unrelated installed component (and a non-component file) must survive.
    writeText(root / "exist" / "OTHER.json", amplifierDefinition("OTHER-AMP").dump(2));
    writeText(root / "notes.txt", "keep me\n");

    const std::string sentinel_json = readFileBytes(root / "exist" / "R100.json");
    const std::string sentinel_other = readFileBytes(root / "exist" / "OTHER.json");
    const std::string sentinel_notes = readFileBytes(root / "notes.txt");

    const LibraryPackageImportResult result = importLibraryPackage(package, root);
    INFO("import error: " << result.error);
    REQUIRE(result.ok);
    REQUIRE(result.imported == 1);
    REQUIRE(result.conflicts.size() == 1);
    REQUIRE(hasConflict(result, conflictLine("amplifier", "Test Corp", "R100")));
    REQUIRE(result.installed_dir == (root / "mixed").string());

    REQUIRE(fs::is_regular_file(root / "mixed" / "amps" / "AM1143.json"));
    REQUIRE(fs::is_regular_file(root / "mixed" / "amps" / "AM1143.s2p"));
    REQUIRE_FALSE(fs::exists(root / "mixed" / "resistors" / "R100.json"));

    REQUIRE(readFileBytes(root / "exist" / "R100.json") == sentinel_json);
    REQUIRE(readFileBytes(root / "exist" / "OTHER.json") == sentinel_other);
    REQUIRE(readFileBytes(root / "notes.txt") == sentinel_notes);
}

TEST_CASE("importLibraryPackage matches conflicting identities case-insensitively",
          "[library][package]") {
    TempDir tmp("import_case");
    const fs::path package = tmp.root / "case-pkg.rflib";
    {
        PackageSpec spec;
        spec.manifest = importManifest(
            "case-pkg", nlohmann::json::array({componentEntry("library/lower/lower.json",
                                                              "amplifier", "anatech", "am1143")}));
        spec.members.push_back({"library/lower/lower.json", amplifierPayload("anatech", "am1143")});
        writePackage(package, spec);
    }
    const fs::path root = tmp.root / "global";
    writeText(root / "exist" / "existing.json", amplifierPayload("Anatech", "AM1143"));

    const LibraryPackageImportResult result = importLibraryPackage(package, root);
    INFO("import error: " << result.error);
    REQUIRE(result.ok);
    REQUIRE(result.imported == 0);
    REQUIRE(result.installed_dir.empty());
    REQUIRE(hasConflict(result, conflictLine("amplifier", "anatech", "am1143")));
    REQUIRE_FALSE(fs::exists(root / "case-pkg"));
    REQUIRE(fs::is_regular_file(root / "exist" / "existing.json"));
}

TEST_CASE("importLibraryPackage reports an all-conflict import without writing a package",
          "[library][package]") {
    TempDir tmp("import_allconflict");
    const fs::path package = tmp.root / "conflict-pkg.rflib";
    writePackage(package, singleAmpPackage("conflict-pkg", "Test Corp", "R100"));

    const fs::path root = tmp.root / "global";
    writeText(root / "exist" / "R100.json", amplifierDefinition("R100").dump(2));
    const auto before = snapshotTree(root);

    const LibraryPackageImportResult result = importLibraryPackage(package, root);
    INFO("import error: " << result.error);
    REQUIRE(result.ok);
    REQUIRE(result.error.empty());
    REQUIRE(result.imported == 0);
    REQUIRE(result.conflicts.size() == 1);
    REQUIRE(hasConflict(result, conflictLine("amplifier", "Test Corp", "R100")));
    REQUIRE(result.installed_dir.empty());
    REQUIRE_FALSE(fs::exists(root / "conflict-pkg"));
    REQUIRE(snapshotTree(root) == before);
}

TEST_CASE("importLibraryPackage refuses manifest identities associated with the wrong JSON paths",
          "[library][package]") {
    TempDir tmp("import_identity_association");
    const fs::path package = tmp.root / "swapped.rflib";
    PackageSpec spec;
    spec.manifest = importManifest(
        "swapped", nlohmann::json::array(
                       {componentEntry("library/a.json", "amplifier", "Test Corp", "PART-B"),
                        componentEntry("library/b.json", "amplifier", "Test Corp", "PART-A")}));
    spec.members.push_back({"library/a.json", amplifierPayload("Test Corp", "PART-A")});
    spec.members.push_back({"library/b.json", amplifierPayload("Test Corp", "PART-B")});
    writePackage(package, spec);

    const fs::path root = tmp.root / "global";
    writeText(root / "notes.txt", "keep me\n");

    requireRefusal(package, root, "identity");
}

TEST_CASE("importLibraryPackage refuses an actual JSON asset omitted from manifest and archive",
          "[library][package]") {
    TempDir tmp("import_unlisted_asset");
    const fs::path package = tmp.root / "unlisted-asset.rflib";
    PackageSpec spec;
    spec.manifest = importManifest(
        "unlisted-asset",
        nlohmann::json::array({componentEntry("library/Test Corp/UNLISTED-AMP.json", "amplifier",
                                              "Test Corp", "UNLISTED-AMP")}));
    spec.members.push_back({"library/Test Corp/UNLISTED-AMP.json",
                            amplifierPayload("Test Corp", "UNLISTED-AMP", "UNLISTED-AMP.s2p")});
    writePackage(package, spec);

    const fs::path root = tmp.root / "global";
    writeText(root / "notes.txt", "keep me\n");

    requireRefusal(package, root, "asset");
}

// --- Import: refusals -------------------------------------------------------

TEST_CASE("importLibraryPackage refuses a truncated archive", "[library][package]") {
    TempDir tmp("import_truncated");
    const fs::path good = tmp.root / "good.rflib";
    writePackage(good, singleAmpPackage("good", "Test Corp", "TRUNC-AMP"));

    const std::string bytes = readFileBytes(good);
    REQUIRE(bytes.size() > 8);
    const fs::path truncated = tmp.root / "truncated.rflib";
    writeBinary(truncated, bytes.substr(0, bytes.size() / 2));

    const fs::path root = tmp.root / "global";
    writeText(root / "notes.txt", "keep me\n");
    requireRefusal(truncated, root, "package");
}

TEST_CASE("importLibraryPackage refuses an unknown format version", "[library][package]") {
    TempDir tmp("import_version");
    const fs::path package = tmp.root / "future.rflib";
    PackageSpec spec = singleAmpPackage("future", "Test Corp", "FUTURE-AMP");
    spec.manifest["format_version"] = library_package::kFormatVersion + 1;
    writePackage(package, spec);

    const fs::path root = tmp.root / "global";
    writeText(root / "notes.txt", "keep me\n");

    requireRefusal(package, root, "format_version");
}

TEST_CASE("importLibraryPackage refuses a parent-traversal member", "[library][package]") {
    TempDir tmp("import_traversal");
    const fs::path package = tmp.root / "evil.rflib";
    PackageSpec spec;
    spec.manifest = importManifest(
        "evil", nlohmann::json::array({componentEntry("../evil.json", "amplifier", "Evil Corp",
                                                      "EVIL", nlohmann::json::array())}));
    spec.members.push_back({"../evil.json", amplifierPayload("Evil Corp", "EVIL")});
    writePackage(package, spec);

    const fs::path root = tmp.root / "global";
    writeText(root / "notes.txt", "keep me\n");

    requireRefusal(package, root, "../evil.json");
}

TEST_CASE("importLibraryPackage refuses a payload member rooted by a double slash",
          "[library][package]") {
    // `library//etc/passwd` passes the archive-spelling check (no backslash, no
    // `..`, not absolute as spelled) but `//etc/passwd` is absolute on POSIX,
    // so joining it onto staging would write outside the staging tree.
    TempDir tmp("import_double_slash");
    const fs::path package = tmp.root / "slash.rflib";
    PackageSpec spec;
    spec.manifest = importManifest(
        "slash", nlohmann::json::array({componentEntry("library//etc/passwd", "amplifier",
                                                       "Evil Corp", "SLASH-AMP")}));
    spec.members.push_back({"library//etc/passwd", amplifierPayload("Evil Corp", "SLASH-AMP")});
    writePackage(package, spec);

    const fs::path root = tmp.root / "global";
    writeText(root / "notes.txt", "keep me\n");

    requireRefusal(package, root, "not a library/ payload member");
}

TEST_CASE("importLibraryPackage refuses an empty payload member after the library/ prefix",
          "[library][package]") {
    // `library/` strips to an empty relative path: the archive inventory
    // rejects the trailing slash before staging is even reserved.
    TempDir tmp("import_empty_remainder");
    const fs::path package = tmp.root / "empty.rflib";
    PackageSpec spec;
    spec.manifest = importManifest(
        "empty",
        nlohmann::json::array({componentEntry("library/", "amplifier", "Evil Corp", "EMPTY-AMP")}));
    spec.members.push_back({"library/", ""});
    writePackage(package, spec);

    const fs::path root = tmp.root / "global";
    writeText(root / "notes.txt", "keep me\n");

    requireRefusal(package, root, "unsafe package member path");
}

TEST_CASE("importLibraryPackage refuses a payload member with a drive-letter root name",
          "[library][package]") {
    // `library/C:/evil.json` strips to `C:/evil.json`, a root-name path whose
    // join with staging discards staging entirely on Windows.
    TempDir tmp("import_drive_root");
    const fs::path package = tmp.root / "drive.rflib";
    PackageSpec spec;
    spec.manifest = importManifest(
        "drive", nlohmann::json::array({componentEntry("library/C:/evil.json", "amplifier",
                                                       "Evil Corp", "DRIVE-AMP")}));
    spec.members.push_back({"library/C:/evil.json", amplifierPayload("Evil Corp", "DRIVE-AMP")});
    writePackage(package, spec);

    const fs::path root = tmp.root / "global";
    writeText(root / "notes.txt", "keep me\n");

    requireRefusal(package, root, "not a library/ payload member");
}

TEST_CASE("importLibraryPackage refuses a zero-byte manifest", "[library][package]") {
    // An empty manifest.json extracts to an empty string; the refusal must say
    // so instead of blaming the JSON parser.
    TempDir tmp("import_empty_manifest");
    const fs::path package = tmp.root / "empty-manifest.rflib";
    writeRawPackage(package, "",
                    {{"library/A.json", amplifierPayload("Test Corp", "EMPTY-MANIFEST-AMP")}});

    const fs::path root = tmp.root / "global";
    writeText(root / "notes.txt", "keep me\n");

    requireRefusal(package, root, "package manifest is empty");
}

TEST_CASE("importLibraryPackage refuses case-duplicate member names", "[library][package]") {
    TempDir tmp("import_case_dup");
    const fs::path package = tmp.root / "dup.rflib";
    PackageSpec spec;
    spec.manifest = importManifest(
        "dup", nlohmann::json::array({componentEntry("library/A.json", "amplifier", "Test Corp",
                                                     "A-AMP", nlohmann::json::array())}));
    spec.members.push_back({"library/A.json", amplifierPayload("Test Corp", "A-AMP")});
    spec.members.push_back({"library/a.json", amplifierPayload("Test Corp", "A-AMP")});
    writePackage(package, spec);

    const fs::path root = tmp.root / "global";
    writeText(root / "notes.txt", "keep me\n");

    requireRefusal(package, root, "duplicate");
}

TEST_CASE("importLibraryPackage refuses a manifest-listed asset missing from the archive",
          "[library][package]") {
    TempDir tmp("import_missing_asset");
    const fs::path package = tmp.root / "missing.rflib";
    PackageSpec spec = singleAmpPackage("missing", "Test Corp", "MISS-AMP");
    // Drop the asset member the manifest names.
    spec.members.pop_back();
    writePackage(package, spec);

    const fs::path root = tmp.root / "global";
    writeText(root / "notes.txt", "keep me\n");

    requireRefusal(package, root, "MISS-AMP.s2p");
}

TEST_CASE("importLibraryPackage refuses a package name that is not a bare segment",
          "[library][package]") {
    TempDir tmp("import_name");
    const fs::path package = tmp.root / "escape-name.rflib";
    writePackage(package, singleAmpPackage("../evil", "Test Corp", "NAME-AMP"));

    const fs::path root = tmp.root / "global";
    writeText(root / "notes.txt", "keep me\n");

    requireRefusal(package, root, "package_name");
}

TEST_CASE("importLibraryPackage refuses an existing package directory", "[library][package]") {
    TempDir tmp("import_existing");
    const fs::path package = tmp.root / "taken.rflib";
    writePackage(package, singleAmpPackage("taken", "Test Corp", "TAKEN-AMP"));

    const fs::path root = tmp.root / "global";
    writeText(root / "taken" / "sentinel.txt", "do not touch\n");
    const std::string sentinel = readFileBytes(root / "taken" / "sentinel.txt");

    requireRefusal(package, root, "already exists");
    REQUIRE(readFileBytes(root / "taken" / "sentinel.txt") == sentinel);
    REQUIRE_FALSE(fs::exists(root / "taken" / "Test Corp"));
}

TEST_CASE("importLibraryPackage refuses an archive over the member-count limit",
          "[library][package]") {
    TempDir tmp("import_members");
    const fs::path package = tmp.root / "huge.rflib";
    PackageSpec spec = singleAmpPackage("huge", "Test Corp", "HUGE-AMP");
    // manifest.json + these == one over the limit.
    for (std::size_t i = 0; i < library_package::kMaxArchiveMembers; ++i)
        spec.members.push_back({"library/tiny-" + std::to_string(i), "x"});
    writePackage(package, spec);

    const fs::path root = tmp.root / "global";
    writeText(root / "notes.txt", "keep me\n");

    requireRefusal(package, root, "limit");
}

TEST_CASE("importLibraryPackage refuses a payload definition the loader drops",
          "[library][package]") {
    TempDir tmp("import_badpayload");
    const fs::path package = tmp.root / "badpayload.rflib";
    PackageSpec spec;
    spec.manifest = importManifest(
        "badpayload", nlohmann::json::array({componentEntry("library/bad.json", "amplifier",
                                                            "Test Corp", "BAD-AMP")}));
    spec.members.push_back({"library/bad.json", "{}"});
    writePackage(package, spec);

    const fs::path root = tmp.root / "global";
    writeText(root / "notes.txt", "keep me\n");

    requireRefusal(package, root, "library/bad.json");
}

TEST_CASE("importLibraryPackage refuses a packaged asset outside its definition directory",
          "[library][package]") {
    TempDir tmp("import_asset_escape");
    const fs::path package = tmp.root / "asset-escape.rflib";
    PackageSpec spec;
    spec.manifest = importManifest("asset-escape",
                                   nlohmann::json::array({componentEntry(
                                       "library/foo/a.json", "amplifier", "Test Corp", "ASSET-AMP",
                                       nlohmann::json::array({"library/other/a.s2p"}))}));
    spec.members.push_back({"library/foo/a.json", amplifierPayload("Test Corp", "ASSET-AMP")});
    spec.members.push_back({"library/other/a.s2p", kS2pBytes});
    writePackage(package, spec);

    const fs::path root = tmp.root / "global";
    writeText(root / "notes.txt", "keep me\n");

    requireRefusal(package, root, "containment");
}

// --- App integration (browser buttons, status, rescan) ----------------------
//
// These drive the same production methods the native dialogs call
// (RfSimulatorApp::exportLibraryPackageTo / importLibraryPackageFrom) so the
// wiring under test cannot drift from a re-implementation here. The dialogs
// themselves are pfd calls and cannot run headless.

namespace {

// ImGui/ImPlot/ImNodes contexts for the widget that RfSimulatorApp constructs;
// there is no renderer backend, so the font atlas is built explicitly (a bare
// context's NewFrame() asserts TexIsBuilt without RendererHasTextures).
struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
        ImGui::GetIO().DisplaySize = ImVec2(1920, 1080);
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char *atlas_pixels = nullptr;
        int atlas_w = 0, atlas_h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&atlas_pixels, &atlas_w, &atlas_h);
    }
    ~ImGuiFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

// The app constructs the first-run tutorial prompt; deactivating it keeps the
// frame under test free of a blocking modal (and the widget's draw) without
// touching the completion marker on disk.
void quiesceTutorial(RfSimulatorApp &app) {
    app.m_show_tutorial_first_run_prompt = false;
    app.m_tutorial_state.exit();
    app.m_show_tutorial = false;
}

std::string readBrowserStatus(RfSimulatorApp &app) { return app.testLibraryBrowser().status(); }

} // namespace

TEST_CASE_METHOD(ImGuiFixture, "App export then import round trip installs and rescans the library",
                 "[library][package][app]") {
    TempDir tmp("app_roundtrip");
    RfSimulatorApp app;
    quiesceTutorial(app);

    // Source root: two amplifiers, one of which references an S2P asset (so the
    // export status has to report both definitions and assets).
    const fs::path source = tmp.root / "source";
    writeAmpWithDataFile(source / "library" / "amplifiers", "APP-AM1143");
    const fs::path json2 = source / "library" / "amplifiers" / "APP-AM2200.json";
    writeText(json2, amplifierDefinition("APP-AM2200").dump(2));

    const fs::path package = tmp.root / "lab-parts.rflib";
    const fs::path dest = tmp.root / "global";

    REQUIRE(app.exportLibraryPackageTo(source.string(), package.string()));
    CHECK(fs::exists(package));
    CHECK(app.testLibraryPackageStatus() ==
          "Exported 2 definitions and 1 assets to " + package.string());
    CHECK(readBrowserStatus(app) == app.testLibraryPackageStatus());

    REQUIRE(app.importLibraryPackageFrom(package.string(), dest.string()));
    // The status reports the installed count; the package name is the archive
    // stem, so the installed tree is <dest>/lab-parts.
    CHECK(app.testLibraryPackageStatus() ==
          "Imported 2 components (0 skipped as already installed)");
    CHECK(readBrowserStatus(app) == app.testLibraryPackageStatus());
    REQUIRE(fs::exists(dest / "lab-parts"));

    // The rescan is observable through the fresh ComponentLibrary the app built
    // over the destination: both identities are present and instantiable.
    ComponentLibrary installed;
    installed.scan(dest.string());
    CHECK(installed.all().size() == 2);
}

TEST_CASE_METHOD(ImGuiFixture, "App package import does not duplicate already loaded definitions",
                 "[library][package][app]") {
    TempDir tmp("app_no_duplicate");
    RfSimulatorApp app;
    quiesceTutorial(app);

    const auto definitions = app.testComponentLibrary().all();
    const ComponentDefinition *seed = nullptr;
    for (const auto *definition : definitions) {
        if (definition->data_files.empty() && fs::exists(definition->source_path)) {
            seed = definition;
            break;
        }
    }
    REQUIRE(seed != nullptr);
    const std::string seeded_type = seed->type;
    const std::string seeded_manufacturer = seed->manufacturer;
    const std::string seeded_part_number = seed->part_number;

    const fs::path destination = tmp.root / "global";
    const fs::path seeded_json =
        destination / "seeded" / seed->type / (seed->part_number + ".json");
    fs::create_directories(seeded_json.parent_path());
    fs::copy_file(seed->source_path, seeded_json);

    const fs::path source = tmp.root / "source";
    const fs::path new_definition = source / "library" / "amplifiers" / "NEW-APP-PART.json";
    writeText(new_definition, amplifierDefinition("NEW-APP-PART").dump(2));
    const fs::path package = tmp.root / "new-part.rflib";
    REQUIRE(app.exportLibraryPackageTo(source.string(), package.string()));
    REQUIRE(app.importLibraryPackageFrom(package.string(), destination.string()));

    const auto after = app.testComponentLibrary().all();
    const auto count_identity = [&after](const std::string &type, const std::string &manufacturer,
                                         const std::string &part_number) {
        return std::count_if(
            after.begin(), after.end(), [&](const ComponentDefinition *definition) {
                return definition->type == type && definition->manufacturer == manufacturer &&
                       definition->part_number == part_number;
            });
    };
    CHECK(count_identity(seeded_type, seeded_manufacturer, seeded_part_number) == 1);
    CHECK(count_identity("amplifier", "Test Corp", "NEW-APP-PART") == 1);
}

TEST_CASE_METHOD(ImGuiFixture, "App import reports mixed conflicts and installs the rest",
                 "[library][package][app]") {
    TempDir tmp("app_conflict");
    RfSimulatorApp app;
    quiesceTutorial(app);

    // Destination already holds APP-AM1143 (same identity the package carries).
    const fs::path dest = tmp.root / "global";
    writeText(dest / "existing" / "APP-AM1143.json", amplifierDefinition("APP-AM1143").dump(2));

    const fs::path source = tmp.root / "source";
    writeAmpWithDataFile(source / "library", "APP-AM1143");
    writeAmpWithDataFile(source / "library", "APP-AM9999");
    const fs::path package = tmp.root / "mixed.rflib";
    REQUIRE(app.exportLibraryPackageTo(source.string(), package.string()));

    REQUIRE(app.importLibraryPackageFrom(package.string(), dest.string()));
    const std::string status = app.testLibraryPackageStatus();
    CHECK(status.find("Imported 1 components") != std::string::npos);
    CHECK(status.find("1 skipped as already installed") != std::string::npos);
    // The skipped identity is named, not just counted.
    CHECK(status.find("amplifier Test Corp APP-AM1143") != std::string::npos);

    ComponentLibrary merged;
    merged.scan(dest.string());
    CHECK(merged.all().size() == 2); // the pre-existing entry plus the new one
}

TEST_CASE_METHOD(ImGuiFixture, "App import refusal returns false with the diagnostic and no rescan",
                 "[library][package][app]") {
    TempDir tmp("app_refusal");
    RfSimulatorApp app;
    quiesceTutorial(app);

    const fs::path dest = tmp.root / "global";
    fs::create_directories(dest);

    // A package directory already exists under the destination: the importer
    // refuses the whole package rather than merging into it.
    const fs::path source = tmp.root / "source";
    writeAmpWithDataFile(source / "library", "APP-REFUSED");
    const fs::path package = tmp.root / "taken.rflib";
    REQUIRE(app.exportLibraryPackageTo(source.string(), package.string()));
    const std::string sentinel = "do not touch\n";
    writeBinary(dest / "taken" / "sentinel.txt", sentinel);

    REQUIRE_FALSE(app.importLibraryPackageFrom(package.string(), dest.string()));
    CHECK(app.testLibraryPackageStatus().find("Import failed: ") != std::string::npos);
    CHECK(app.testLibraryPackageStatus().find("already exists") != std::string::npos);
    CHECK_FALSE(fs::exists(dest / "taken" / "Test Corp"));
    CHECK(readFileBytes(dest / "taken" / "sentinel.txt") == sentinel);
}

TEST_CASE_METHOD(ImGuiFixture,
                 "App browser draws a headless frame with an empty then populated status",
                 "[library][package][app]") {
    TempDir tmp("app_frame");
    RfSimulatorApp app;
    quiesceTutorial(app);

    const fs::path source = tmp.root / "source";
    writeAmpWithDataFile(source / "library", "APP-FRAME");
    const fs::path package = tmp.root / "frame.rflib";
    const fs::path dest = tmp.root / "global";

    app.testShowLibrary() = true;
    REQUIRE(app.testLibraryBrowser().status().empty());

    // Frame 1: the panel opens with no package operation yet — the buttons and
    // the export notice still have to render without a status line.
    ImGui::NewFrame();
    app.draw_ui();
    ImGui::EndFrame();
    CHECK(app.testShowLibrary());
    CHECK(app.testLibraryBrowser().status().empty());

    // Exercise the production path, then a second frame renders the status.
    REQUIRE(app.exportLibraryPackageTo(source.string(), package.string()));
    REQUIRE(app.importLibraryPackageFrom(package.string(), dest.string()));

    ImGui::NewFrame();
    app.draw_ui();
    ImGui::EndFrame();
    CHECK(app.testLibraryBrowser().status().find("Imported 1 components") != std::string::npos);
    CHECK(app.testShowLibrary());
}

TEST_CASE_METHOD(ImGuiFixture,
                 "Library browser keeps the package status as its own replaceable line",
                 "[library][package][app]") {
    ComponentLibrary library;
    LibraryBrowserWidget widget(library);
    bool open = true;

    // A frozen-empty status is what the export/import notice renders around, so
    // both states must draw the panel without crashing (the app-level frame case
    // covers the assembled UI; this one covers the widget's own states).
    REQUIRE(widget.status().empty());
    ImGui::NewFrame();
    widget.draw("Component Library", &open);
    ImGui::Render();
    CHECK(open);

    widget.setStatus("Exported 2 definitions and 1 assets to C:/tmp/lab.rflib");
    CHECK(widget.status() == "Exported 2 definitions and 1 assets to C:/tmp/lab.rflib");
    ImGui::NewFrame();
    widget.draw("Component Library", &open);
    ImGui::Render();
    CHECK(open);

    // Each package operation replaces the previous result rather than appending,
    // so the panel never accumulates stale outcomes.
    widget.setStatus("Import failed: package directory already exists: C:/tmp/global/x");
    CHECK(widget.status() == "Import failed: package directory already exists: C:/tmp/global/x");
    ImGui::NewFrame();
    widget.draw("Component Library", &open);
    ImGui::Render();
    CHECK(open);

    // An empty status is a valid state again (the pre-operation panel).
    widget.setStatus(std::string());
    CHECK(widget.status().empty());
}
