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

#include "component_library.h"
#include "library_package.h"
#include "test_temp_paths.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <miniz.h>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>
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

TEST_CASE("exportLibraryPackage refuses an escaping referenced asset", "[library][package]") {
    TempDir tmp("escape");
    const fs::path root = tmp.root / "root";
    const fs::path outside = root / "outside.s2p";
    writeS2p(outside);
    writeText(root / "amp.json",
              amplifierDefinition(
                  "ESCAPE-AMP",
                  {{"data_files", nlohmann::json::array(
                                      {{{"type", "s_parameters"}, {"path", "../outside.s2p"}}})}})
                  .dump(2));
    REQUIRE(fs::exists(outside)); // the file exists; it escapes the definition directory

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
