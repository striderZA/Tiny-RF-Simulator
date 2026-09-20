// Standalone Catch2 executable for the S1/S2 security fixes from the
// 2026-08-09 codebase review, plus the issue #120 component-authoring save
// path:
//   S1 — S-param file path containment (project-file boundary in
//        ProjectSerializer::load()/save(), library boundary in
//        ComponentLibrary::instantiate())
//   S2 — touchstone parser file-size guard + in-loop frequency-point cap
//   #120 — the S-param file the authoring form copies next to the library JSON
//        is named from a sanitized part number, and the save path refuses any
//        data-file name that is not a bare file name (no separators, no '..').
//
// Built as its own executable rather than appended to the main `tests` binary
// because this MinGW-w64 toolchain silently drops any TEST_CASE registered
// beyond the ceiling in tests.exe (see the comment above test_component_authoring
// in tests/CMakeLists.txt).
#include "amplifier_engine.h"
#include "app.h"
#include "component_form_model.h"
#include "component_library.h"
#include "component_registry.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "node_graph_engine.h"
#include "test_temp_paths.h"
#include "touchstone_parser.h"
#include "view_manager.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace {

struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
    }
    ~ImGuiFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

std::filesystem::path scratchDir(const std::string &name) {
    auto base = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(base);
    return base;
}

// Minimal valid 2-port Touchstone file (freq + S11, S21, S12, S22 in MA).
const char *kMinimalS2p = "# GHz S MA R 50\n"
                          "1.0 0.5 0.0 2.0 90.0 0.1 180.0 0.3 -45.0\n";

void writeS2p(const std::filesystem::path &path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream ofs(path);
    ofs << kMinimalS2p;
}

nlohmann::json amplifierComponent(const std::string &sparam_filepath) {
    nlohmann::json cj;
    cj["type"] = "Amplifier"; // .rfsim project_type key
    cj["params"]["gain_dB"] = 20.0;
    cj["params"]["nf_dB"] = 1.0;
    cj["params"]["sparam_mode"] = true;
    cj["params"]["sparam_filepath"] = sparam_filepath;
    return cj;
}

void writeProject(const std::filesystem::path &path, const nlohmann::json &components) {
    nlohmann::json root;
    root["version"] = 1;
    root["components"] = components;
    std::ofstream ofs(path);
    ofs << root.dump(2);
}

} // namespace

// ---------------------------------------------------------------------------
// S1 — project-file boundary: untrusted S-param paths must not read files
// outside the project directory. Absolute paths outside the project dir and
// '..' traversal are neutralized (path cleared, nothing loaded); a contained
// relative path resolves against the project dir (not the CWD) and loads.
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Project load neutralizes S-param paths outside the project dir",
                 "[containment][project]") {
    auto base = scratchDir("containment_project_test");
    const auto project_path = base / "proj.rfsim";
    const auto decoy = base.parent_path() / "containment_project_decoy.s2p";
    writeS2p(decoy); // valid file OUTSIDE the project dir
    writeS2p(base / "data/good.s2p");

    nlohmann::json comps = nlohmann::json::array();
    comps.push_back(amplifierComponent(decoy.string()));     // absolute, outside project dir
    comps.push_back(amplifierComponent("../../escape.s2p")); // '..' traversal
    comps.push_back(amplifierComponent("data/good.s2p"));    // contained relative path
    writeProject(project_path, comps);

    {
        RfSimulatorApp app;
        app.loadProject(project_path.string());
        REQUIRE(app.componentCount() == 3);

        auto amps = app.testComponents().byType<AmplifierEngine>();
        REQUIRE(amps.size() == 3);

        // Absolute path outside the project dir: neutralized, nothing loaded.
        CHECK_FALSE(amps[0]->sparamLoaded());
        CHECK(amps[0]->sparamFilepath().empty());
        CHECK_FALSE(amps[0]->sparamMode());

        // '..' traversal: neutralized, nothing loaded.
        CHECK_FALSE(amps[1]->sparamLoaded());
        CHECK(amps[1]->sparamFilepath().empty());
        CHECK_FALSE(amps[1]->sparamMode());

        // Contained relative path: resolved against the project dir and loaded
        // (the CWD has no data/good.s2p, so this only works if the resolution
        // uses the project dir).
        CHECK(amps[2]->sparamLoaded());
        const auto expected = std::filesystem::weakly_canonical(base / "data/good.s2p").string();
        CHECK(amps[2]->sparamFilepath() == expected);
    }
    std::filesystem::remove_all(base);
    std::filesystem::remove(decoy);
}

TEST_CASE_METHOD(ImGuiFixture, "Project save persists in-project S-param paths as relative",
                 "[containment][project]") {
    auto base = scratchDir("containment_roundtrip_test");
    const auto project_path = base / "proj.rfsim";
    writeS2p(base / "data/good.s2p");

    // Absolute path that stays inside the project dir is honored on load...
    nlohmann::json comps = nlohmann::json::array();
    comps.push_back(
        amplifierComponent(std::filesystem::weakly_canonical(base / "data/good.s2p").string()));
    writeProject(project_path, comps);

    {
        RfSimulatorApp app;
        app.loadProject(project_path.string());
        auto amps = app.testComponents().byType<AmplifierEngine>();
        REQUIRE(amps.size() == 1);
        REQUIRE(amps[0]->sparamLoaded());

        // ...but re-saving re-writes it relative to the project dir, so the
        // file stays portable between machines.
        app.saveProject(project_path.string());
    }

    nlohmann::json saved;
    {
        std::ifstream ifs(project_path);
        ifs >> saved;
    }
    REQUIRE(saved["components"][0]["params"]["sparam_filepath"] == "data/good.s2p");

    // The relative path round-trips: reload from the same file still loads.
    {
        RfSimulatorApp app;
        app.loadProject(project_path.string());
        auto amps = app.testComponents().byType<AmplifierEngine>();
        REQUIRE(amps.size() == 1);
        REQUIRE(amps[0]->sparamLoaded());
    }
    std::filesystem::remove_all(base);
}

// ---------------------------------------------------------------------------
// S1 — library boundary: data_files entries must stay within the library JSON
// file's directory; absolute and '..' entries are skipped, not read.
// ---------------------------------------------------------------------------
TEST_CASE("Library instantiate skips S-param data files outside the library dir",
          "[containment][library]") {
    auto base = scratchDir("containment_lib_test");
    const auto json_path = base / "lib.json";
    const auto decoy = base.parent_path() / "containment_lib_decoy.s2p";
    writeS2p(decoy); // valid file OUTSIDE the library dir
    writeS2p(base / "good.s2p");

    nlohmann::json j;
    j["schema_version"] = 2;
    j["type"] = "amplifier";
    j["part_number"] = "CONTAINMENT-LIB";
    j["parameters"]["gain_dB"] = 20.0;
    j["parameters"]["nf_dB"] = 1.0;
    j["data_files"] = nlohmann::json::array();
    j["data_files"].push_back({{"type", "s_parameters"}, {"path", "../containment_lib_decoy.s2p"}});
    j["data_files"].push_back({{"type", "s_parameters"}, {"path", decoy.string()}});
    j["data_files"].push_back({{"type", "s_parameters"}, {"path", "good.s2p"}});
    {
        std::ofstream ofs(json_path);
        ofs << j.dump(2);
    }

    ComponentLibrary lib;
    lib.loadFile(json_path.string());
    auto defs = lib.all();
    REQUIRE(defs.size() == 1);

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);
    auto *engine = lib.instantiate(*defs[0], 400, registry, graph);
    REQUIRE(engine != nullptr);

    auto *amp = dynamic_cast<AmplifierEngine *>(engine);
    REQUIRE(amp != nullptr);
    // The two rejected entries are skipped; only the contained one loads.
    REQUIRE(amp->sparamLoaded());
    CHECK(amp->sparamFilepath() == std::filesystem::weakly_canonical(base / "good.s2p").string());

    std::filesystem::remove_all(base);
    std::filesystem::remove(decoy);
}

TEST_CASE("Library instantiate rejects when every S-param data file escapes",
          "[containment][library]") {
    auto base = scratchDir("containment_lib_reject_test");
    const auto json_path = base / "lib.json";
    const auto decoy = base.parent_path() / "containment_lib_reject_decoy.s2p";
    writeS2p(decoy); // valid file OUTSIDE the library dir

    nlohmann::json j;
    j["schema_version"] = 2;
    j["type"] = "amplifier";
    j["part_number"] = "CONTAINMENT-REJECT";
    j["parameters"]["gain_dB"] = 20.0;
    j["parameters"]["nf_dB"] = 1.0;
    j["data_files"] = nlohmann::json::array();
    j["data_files"].push_back({{"type", "s_parameters"}, {"path", decoy.string()}});
    j["data_files"].push_back({{"type", "s_parameters"}, {"path", "../escape.s2p"}});
    {
        std::ofstream ofs(json_path);
        ofs << j.dump(2);
    }

    ComponentLibrary lib;
    lib.loadFile(json_path.string());
    auto defs = lib.all();
    REQUIRE(defs.size() == 1);

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);
    auto *engine = lib.instantiate(*defs[0], 401, registry, graph);
    REQUIRE(engine != nullptr);

    auto *amp = dynamic_cast<AmplifierEngine *>(engine);
    REQUIRE(amp != nullptr);
    // Nothing was loaded: the engine falls back to single-point params.
    CHECK_FALSE(amp->sparamLoaded());
    CHECK_FALSE(amp->sparamMode());
    CHECK(amp->gain_dB() == Catch::Approx(20.0));

    std::filesystem::remove_all(base);
    std::filesystem::remove(decoy);
}

// ---------------------------------------------------------------------------
// S2 — touchstone parser: a file larger than the 256 MiB guard is rejected
// before its content is buffered. The same content parses fine at a small
// size, proving the size guard (not the content) is what rejects it.
// ---------------------------------------------------------------------------
TEST_CASE("TouchstoneParser rejects oversized files before reading", "[containment][touchstone]") {
    const auto path = std::filesystem::temp_directory_path() / "containment_oversized.s2p";
    writeS2p(path);

    // Content is valid at a normal size...
    auto before = TouchstoneParser::parse(path.string());
    REQUIRE(before.has_value());

    // ...extend the file past the 256 MiB parse cap. resize_file extends
    // without writing data (cheap on NTFS; zero-fill on other filesystems).
    std::error_code ec;
    std::filesystem::resize_file(path, 300LL * 1024 * 1024, ec);
    REQUIRE_FALSE(ec);

    auto after = TouchstoneParser::parse(path.string());
    REQUIRE_FALSE(after.has_value());

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Issue #120 — component-authoring save path: the S-param file copied next to
// the library JSON is named from `sanitizePathSegment(part_number)` instead of
// the raw part number, and the app refuses any data-file name that is not a
// bare file name before joining it onto the destination directory.
// ---------------------------------------------------------------------------
TEST_CASE("Data-file names that could escape the library root are rejected",
          "[containment][issue120][library]") {
    const std::string dir = "library/amplifier/acme";
    // Traversal / separator / absolute spellings, refused on both platforms so
    // a library authored on Windows is as safe read on Linux (and vice versa).
    // This is the exact composition RfSimulatorApp::saveComponentForm() makes
    // for a new entry.
    CHECK_FALSE(dataFileCopyDestination(dir, "..\\..\\evil.s2p").has_value());
    CHECK_FALSE(dataFileCopyDestination(dir, "../../evil.s2p").has_value());
    CHECK_FALSE(dataFileCopyDestination(dir, "sub/evil.s2p").has_value());
    CHECK_FALSE(dataFileCopyDestination(dir, "sub\\evil.s2p").has_value());
    CHECK_FALSE(dataFileCopyDestination(dir, "/abs/evil.s2p").has_value());
    CHECK_FALSE(dataFileCopyDestination(dir, "\\abs\\evil.s2p").has_value());
    CHECK_FALSE(dataFileCopyDestination(dir, "C:/evil.s2p").has_value());
    CHECK_FALSE(dataFileCopyDestination(dir, "C:\\evil.s2p").has_value());
    CHECK_FALSE(dataFileCopyDestination(dir, "..").has_value());
    CHECK_FALSE(dataFileCopyDestination(dir, ".").has_value());
    CHECK_FALSE(dataFileCopyDestination(dir, "").has_value());
    CHECK_FALSE(dataFileCopyDestination("", "AMP.s2p").has_value());

    // The plain names the authoring form produces resolve next to the JSON.
    CHECK(dataFileCopyDestination(dir, "AM1143.s2p") == std::filesystem::path(dir) / "AM1143.s2p");
    CHECK(dataFileCopyDestination(dir, "ZX60-33LN.s2p").has_value());
    CHECK(dataFileCopyDestination(dir, "Test Amp.s2p").has_value());
    // ".." that is part of a name rather than a path component stays legal —
    // the gate tests path components, not a ".." substring.
    CHECK(dataFileCopyDestination(dir, "AMP..v2.s2p").has_value());
}

TEST_CASE("Component authoring derives the copied S-param name from a sanitized part number",
          "[containment][issue120]") {
    const auto *descriptor = ComponentTypeRegistry::instance().find("amplifier");
    REQUIRE(descriptor != nullptr);

    ComponentFormModel model(*descriptor);
    model.setPartNumber("..\\..\\evil");
    model.setManufacturer("Acme");
    model.setParameter("gain_dB", 20.0);
    model.setSparamSourcePath("AM1143.s2p"); // extension is all that is read

    const auto def = model.buildDefinition();
    REQUIRE(def.data_files.size() == 1);
    CHECK(def.data_files.front().type == "s_parameters");
    CHECK(def.data_files.front().path == "evil.s2p");
    CHECK(
        dataFileCopyDestination("library/amplifier/Acme", def.data_files.front().path).has_value());

    // The pre-fix spelling of this entry's data file — the raw part number plus
    // the picked extension, which is what the JSON would then have persisted —
    // is exactly what the gate refuses, so the stored path can never disagree
    // with the copied file name.
    CHECK_FALSE(
        dataFileCopyDestination("library/amplifier/Acme", model.partNumber() + ".s2p").has_value());

    // A part number that sanitizes to nothing falls back to "component" instead
    // of producing an extension-only or empty file name.
    model.setPartNumber("..");
    CHECK(model.buildDefinition().data_files.front().path == "component.s2p");
}

TEST_CASE("Component authoring copy destination stays inside the library root",
          "[containment][issue120]") {
    namespace fs = std::filesystem;
    const auto base =
        fs::temp_directory_path() / ("containment_issue120_" + test_temp_paths::processTag());
    fs::remove_all(base);
    fs::create_directories(base);
    const auto picked = base / "picked.s2p";
    writeS2p(picked);

    const auto *descriptor = ComponentTypeRegistry::instance().find("amplifier");
    REQUIRE(descriptor != nullptr);
    ComponentFormModel model(*descriptor);
    model.setPartNumber("..\\..\\escape");
    model.setParameter("gain_dB", 20.0);
    model.setParameter("nf_dB", 2.0);
    model.setSparamSourcePath(picked.string());
    const auto def = model.buildDefinition();
    REQUIRE(def.data_files.size() == 1);

    // Same composition as RfSimulatorApp::saveComponentForm() for a new entry
    // (the app method itself is private): the data-file name is joined onto the
    // JSON's directory through the shared safety gate.
    const fs::path root = base / "library";
    const fs::path dir = root / def.type / "unknown";
    fs::create_directories(dir);
    const fs::path json_path = dir / "escape.json";
    const auto dest_sparam =
        dataFileCopyDestination(json_path.parent_path().string(), def.data_files.front().path);
    REQUIRE(dest_sparam.has_value());
    std::error_code ec;
    fs::copy_file(model.sparamSourcePath(), *dest_sparam, fs::copy_options::overwrite_existing, ec);
    REQUIRE_FALSE(ec);

    // The copy landed next to the JSON, inside the library root...
    CHECK(fs::weakly_canonical(*dest_sparam) == fs::weakly_canonical(dir / "escape.s2p"));
    // ...and nothing was written to the traversal target the raw part number
    // ("..\..\escape.s2p" from directory/type/manufacturer) would have reached.
    CHECK_FALSE(fs::exists(root / "escape.s2p"));
    CHECK(fs::exists(dir / "escape.s2p"));
    // The name the JSON persists is the name that was copied, so the entry stays
    // resolvable on the next load instead of pointing at the raw part number.
    CHECK(dest_sparam->filename().string() == def.data_files.front().path);
    CHECK(def.data_files.front().path.find('\\') == std::string::npos);
    CHECK(def.data_files.front().path.find('/') == std::string::npos);

    // The sanitized name is the one the library later resolves: write the entry
    // the way saveComponentForm() does and instantiate it, so the fixed naming
    // is proven usable end to end (not just traversal-proof).
    nlohmann::json entry;
    entry["schema_version"] = 2;
    entry["type"] = def.type;
    entry["part_number"] = def.part_number;
    entry["parameters"] = def.parameters;
    entry["data_files"] = nlohmann::json::array();
    entry["data_files"].push_back(
        {{"type", def.data_files.front().type}, {"path", def.data_files.front().path}});
    {
        std::ofstream ofs(json_path);
        ofs << entry.dump(2);
    }

    ComponentLibrary lib;
    lib.loadFile(json_path.string());
    REQUIRE(lib.all().size() == 1);
    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);
    auto *engine = lib.instantiate(*lib.all().front(), 402, registry, graph);
    REQUIRE(engine != nullptr);
    auto *amp = dynamic_cast<AmplifierEngine *>(engine);
    REQUIRE(amp != nullptr);
    CHECK(amp->sparamLoaded());

    fs::remove_all(base);
}
