// Standalone Catch2 executable for the S1/S2 security fixes from the
// 2026-08-09 codebase review, plus the issue #120 and #184 component-authoring
// save paths:
//   S1 — S-param file path containment (project-file boundary in
//        ProjectSerializer::load()/save(), library boundary in
//        ComponentLibrary::instantiate())
//   S2 — touchstone parser file-size guard + in-loop frequency-point cap
//   #120 — the copied S-param file is named from a sanitized part number and
//          save refuses a data-file name that is not a bare file name.
//   #184 — save refuses to overwrite another component's asset; an edit may
//          replace its own file only when no other loaded definition references it.
// The copy cases drive the real RfSimulatorApp::saveComponentForm() through test
// accessors, so the app's own destination composition is what is exercised.
//
// Built as its own executable rather than appended to the main `tests` binary
// because this MinGW-w64 toolchain silently drops any TEST_CASE registered
// beyond the ceiling in tests.exe (see the comment above test_component_authoring
// in tests/CMakeLists.txt).
#include "amplifier_engine.h"
#include "app.h"
#include "circuit_runtime.h"
#include "component_form_model.h"
#include "component_library.h"
#include "graph_editor_actions.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "node_graph_engine.h"
#include "test_temp_paths.h"
#include "touchstone_parser.h"
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

// Reads a file's bytes verbatim, so a case can assert that what was copied is
// what was picked (and that an edit replaced the previous revision in place).
std::string readFile(const std::filesystem::path &path) {
    std::ifstream ifs(path, std::ios::binary);
    std::string out;
    char buf[4096];
    while (ifs) {
        ifs.read(buf, sizeof buf);
        out.append(buf, static_cast<std::string::size_type>(ifs.gcount()));
    }
    return out;
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
#ifndef TEST_EXTERNAL_PROJECT_DATA_ONLY
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
// Legacy `sparam_path` key: a project saved before the `sparam_filepath` rename
// carries only `sparam_path`. Project load resolves it against the project dir,
// and the engine must then load the file it names.
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Project load loads an in-project legacy-only sparam_path",
                 "[containment][project][legacy]") {
    auto base = scratchDir("containment_legacy_key_" + test_temp_paths::processTag());
    const auto project_path = base / "proj.rfsim";
    writeS2p(base / "data/legacy.s2p");

    nlohmann::json amp;
    amp["type"] = "Amplifier"; // .rfsim project_type key
    amp["params"]["gain_dB"] = 20.0;
    amp["params"]["sparam_mode"] = true;
    amp["params"]["sparam_path"] = "data/legacy.s2p"; // legacy key only
    writeProject(project_path, nlohmann::json::array({amp}));

    {
        RfSimulatorApp app;
        app.loadProject(project_path.string());
        REQUIRE(app.componentCount() == 1);

        auto amps = app.testComponents().byType<AmplifierEngine>();
        REQUIRE(amps.size() == 1);
        CHECK(amps[0]->sparamLoaded());
        CHECK(amps[0]->sparamMode());
        const auto expected = std::filesystem::weakly_canonical(base / "data/legacy.s2p").string();
        CHECK(amps[0]->sparamFilepath() == expected);
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

    CircuitRuntime runtime;
    GraphEditorActions actions(runtime);
    auto *engine = lib.instantiate(*defs[0], runtime, actions);
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

    CircuitRuntime runtime;
    GraphEditorActions actions(runtime);
    auto *engine = lib.instantiate(*defs[0], runtime, actions);
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

TEST_CASE_METHOD(ImGuiFixture, "Component authoring save keeps the copied S-param inside the root",
                 "[containment][issue120][issue184]") {
    namespace fs = std::filesystem;
    const auto base =
        fs::temp_directory_path() / ("containment_issue120_" + test_temp_paths::processTag());
    fs::remove_all(base);
    fs::create_directories(base);
    const auto picked = base / "picked.s2p";
    writeS2p(picked);
    const fs::path root = base / "library";

    // Drives the real RfSimulatorApp::saveComponentForm() through the app's test
    // accessors: the composition of dest_dir and the data-file name under test
    // is the app's own, so it cannot drift from a re-implementation here.
    RfSimulatorApp app;
    app.testOpenNewComponentForm("amplifier", root.string());
    auto &model = app.testComponentFormModel();
    model.setPartNumber("..\\..\\escape");
    model.setManufacturer("Acme");
    model.setParameter("gain_dB", 20.0);
    model.setParameter("nf_dB", 2.0);
    model.setSparamSourcePath(picked.string());
    REQUIRE(app.testSaveComponentForm());

    // The part number names neither the entry's directory nor its data file: the
    // save lands under the sanitized "escape" spelling.
    const fs::path dir = root / "amplifier" / "Acme";
    const fs::path json_path = dir / "escape.json";
    REQUIRE(fs::exists(json_path));
    REQUIRE(fs::exists(dir / "escape.s2p"));
    CHECK(readFile(dir / "escape.s2p") == readFile(picked));
    // The traversal target the raw name ("..\..\escape.s2p" from dir) would have
    // reached stays empty.
    CHECK_FALSE(fs::exists(root / "escape.s2p"));

    // The name the JSON persists is the name that was copied, so the entry stays
    // resolvable on the next load instead of pointing at the raw part number.
    const nlohmann::json entry = nlohmann::json::parse(readFile(json_path));
    REQUIRE(entry.contains("data_files"));
    REQUIRE(entry["data_files"].size() == 1);
    CHECK(entry["data_files"][0]["type"] == "s_parameters");
    CHECK(entry["data_files"][0]["path"] == "escape.s2p");
    CHECK(entry["part_number"] == "..\\..\\escape");

    // ...and the library resolves that name to the copied file: instantiating the
    // saved entry proves the fixed naming is usable end to end, not just
    // traversal-proof.
    ComponentLibrary lib;
    lib.loadFile(json_path.string());
    REQUIRE(lib.all().size() == 1);
    CircuitRuntime runtime;
    GraphEditorActions actions(runtime);
    auto *engine = lib.instantiate(*lib.all().front(), runtime, actions);
    REQUIRE(engine != nullptr);
    auto *amp = dynamic_cast<AmplifierEngine *>(engine);
    REQUIRE(amp != nullptr);
    CHECK(amp->sparamLoaded());

    fs::remove_all(base);
}

TEST_CASE_METHOD(ImGuiFixture,
                 "Component authoring edit overwrites in place, never under a destination root",
                 "[containment][issue120][issue184]") {
    namespace fs = std::filesystem;
    const auto base =
        fs::temp_directory_path() / ("containment_issue120_edit_" + test_temp_paths::processTag());
    fs::remove_all(base);
    const fs::path root = base / "library";
    const fs::path dir = root / "amplifier" / "Acme";
    fs::create_directories(dir);
    const fs::path json_path = dir / "escape.json";
    const fs::path data_path = dir / "escape.s2p";

    // A previously saved entry with its data file already beside its JSON.
    {
        nlohmann::json entry;
        entry["schema_version"] = 2;
        entry["type"] = "amplifier";
        entry["part_number"] = "..\\..\\escape";
        entry["manufacturer"] = "Acme";
        entry["parameters"] = {{"gain_dB", 20.0}, {"nf_dB", 2.0}};
        entry["data_files"] = nlohmann::json::array();
        entry["data_files"].push_back({{"type", "s_parameters"}, {"path", data_path.string()}});
        std::ofstream ofs(json_path);
        ofs << entry.dump(2);
    }
    { // an older revision of the data file, so overwrite-in-place is observable
        std::ofstream ofs(data_path);
        ofs << "# GHz S MA R 50\n1.0 0.9 0.0 2.0 90.0 0.1 180.0 0.3 -45.0\n";
    }
    const auto picked = base / "picked.s2p";
    writeS2p(picked);

    ComponentLibrary lib;
    lib.loadFile(json_path.string());
    REQUIRE(lib.all().size() == 1);
    const ComponentDefinition def = *lib.all().front();

    RfSimulatorApp app;
    app.testOpenEditComponentForm(def);
    // An edit overwrites the entry's own directory (dest_dir comes from the
    // entry's source_path), so a destination root picked earlier in the session
    // is never a save target and stays uncreated.
    const fs::path unused_root = base / "chosen-root";
    app.testSetComponentFormDestinationRoot(unused_root.string());
    app.testComponentFormModel().setSparamSourcePath(picked.string());
    REQUIRE(app.testSaveComponentForm());

    CHECK(readFile(data_path) == readFile(picked)); // replaced in place
    CHECK_FALSE(fs::exists(unused_root));
    CHECK_FALSE(fs::exists(root / "escape.s2p"));

    ComponentLibrary reloaded;
    reloaded.loadFile(json_path.string());
    REQUIRE(reloaded.all().size() == 1);
    REQUIRE(reloaded.all().front()->data_files.size() == 1);
    CHECK(reloaded.all().front()->data_files.front().path == "escape.s2p");

    fs::remove_all(base);
}

TEST_CASE_METHOD(
    ImGuiFixture,
    "Component authoring refuses to overwrite another component's S-param asset (issue #184)",
    "[containment][issue184]") {
    namespace fs = std::filesystem;
    const fs::path base =
        fs::temp_directory_path() / ("issue184_asset_collision_" + test_temp_paths::processTag());
    fs::remove_all(base);
    const fs::path dir = base / "library" / "amplifier" / "Acme";
    fs::create_directories(dir);
    const fs::path sibling_json = dir / "SIBLING.json";
    const fs::path sibling_asset = dir / "SIBLING.s2p";
    const std::string sibling_asset_contents = "sibling component's original asset";
    {
        nlohmann::json sibling;
        sibling["schema_version"] = 2;
        sibling["type"] = "amplifier";
        sibling["part_number"] = "SIBLING";
        sibling["manufacturer"] = "Acme";
        sibling["parameters"] = {{"gain_dB", 20.0}};
        sibling["data_files"] = {{{"type", "s_parameters"}, {"path", "SIBLING.s2p"}}};
        std::ofstream json_file(sibling_json);
        json_file << sibling.dump(2);
        std::ofstream asset_file(sibling_asset);
        asset_file << sibling_asset_contents;
    }
    const fs::path picked = base / "picked.s2p";
    writeS2p(picked);

    ComponentDefinition edit;
    edit.schema_version = 2;
    edit.type = "amplifier";
    edit.part_number = "EDIT-ME";
    edit.manufacturer = "Acme";
    edit.parameters = {{"gain_dB", 20.0}};
    edit.source_path = (dir / "EDIT-ME.json").string();

    RfSimulatorApp app;
    app.testOpenEditComponentForm(edit);
    app.testComponentFormModel().setPartNumber("SIBLING");
    app.testComponentFormModel().setSparamSourcePath(picked.string());

    CHECK_FALSE(app.testSaveComponentForm());
    CHECK(readFile(sibling_asset) == sibling_asset_contents);
    CHECK_FALSE(fs::exists(edit.source_path));
    fs::remove_all(base);
}

TEST_CASE_METHOD(ImGuiFixture,
                 "Component authoring preserves an edited S-param asset when definition save fails",
                 "[containment][issue184]") {
    namespace fs = std::filesystem;
    const fs::path base =
        fs::temp_directory_path() / ("issue184_failed_save_" + test_temp_paths::processTag());
    fs::remove_all(base);
    const fs::path dir = base / "library" / "amplifier" / "Acme";
    fs::create_directories(dir);

    const fs::path definition_path = dir / "EDIT-ME.json";
    fs::create_directories(definition_path); // make opening the JSON target fail deterministically
    const fs::path asset_path = dir / "EDIT-ME.s2p";
    const std::string original_asset = "original S-parameter asset";
    {
        std::ofstream asset(asset_path);
        asset << original_asset;
    }
    const fs::path picked = base / "picked.s2p";
    writeS2p(picked);

    ComponentDefinition edit;
    edit.schema_version = 2;
    edit.type = "amplifier";
    edit.part_number = "EDIT-ME";
    edit.manufacturer = "Acme";
    edit.parameters = {{"gain_dB", 20.0}};
    edit.source_path = definition_path.string();
    edit.data_files = {{"s_parameters", "EDIT-ME.s2p"}};

    RfSimulatorApp app;
    app.testOpenEditComponentForm(edit);
    app.testComponentFormModel().setSparamSourcePath(picked.string());

    CHECK_FALSE(app.testSaveComponentForm());
    CHECK(readFile(asset_path) == original_asset);
    fs::remove_all(base);
}

TEST_CASE("componentDataFilePath resolves contained data files and refuses escapes",
          "[containment][library][data_file]") {
    const auto base = scratchDir("containment_accessor_" + test_temp_paths::processTag());
    const std::string decoy_name =
        "containment_accessor_decoy_" + test_temp_paths::processTag() + ".s2p";
    writeS2p(base / "good.s2p");
    writeS2p(base.parent_path() / decoy_name);

    // One library JSON per case, written the way the library containment tests above do.
    const auto load = [&](const std::string &part_number, const std::string &data_path) {
        nlohmann::json j;
        j["schema_version"] = 2;
        j["type"] = "amplifier";
        j["part_number"] = part_number;
        j["parameters"]["gain_dB"] = 20.0;
        j["parameters"]["nf_dB"] = 1.0;
        j["data_files"] = nlohmann::json::array();
        j["data_files"].push_back({{"type", "s_parameters"}, {"path", data_path}});
        const auto json_path = base / (part_number + ".json");
        {
            std::ofstream ofs(json_path);
            ofs << j.dump(2);
        }
        ComponentLibrary lib;
        lib.loadFile(json_path.string());
        REQUIRE(lib.all().size() == 1);
        return *lib.all().front();
    };

    const auto contained = load("ACCESSOR-OK", "good.s2p");
    const auto found = componentDataFilePath(contained, "s_parameters");
    REQUIRE(found.has_value());
    CHECK(*found == std::filesystem::weakly_canonical(base / "good.s2p"));
    CHECK_FALSE(componentDataFilePath(contained, "gain").has_value());

    const auto escaping = load("ACCESSOR-ESCAPE", "../" + decoy_name);
    CHECK_FALSE(componentDataFilePath(escaping, "s_parameters").has_value());
}

#endif // !TEST_EXTERNAL_PROJECT_DATA_ONLY
// ---------------------------------------------------------------------------
// AMP intercept files (amp_filepath) use the same project-file boundary as
// S-param files: a path outside the project dir or with '..' is neutralized, a
// contained relative path loads, and saving writes it relative.
// ---------------------------------------------------------------------------
namespace {

// A valid AMP file: one network section (the MathWorks rule for S, Y, Z, or POUT
// data) and one OIP3 section.
const char *kMinimalAmp = "S RI R 50\nFREQ GHZ\n1.0 0.5 0.0 2.0 0.0 0.0 0.0 0.3 0.0\n"
                          "OIP3 dBm\nFREQ GHZ\n1.0 30.0\n2.0 31.0\n";

void writeAmp(const std::filesystem::path &path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream ofs(path);
    ofs << kMinimalAmp;
}

nlohmann::json ampComponent(const std::string &amp_filepath) {
    nlohmann::json cj;
    cj["type"] = "Amplifier"; // .rfsim project_type key
    cj["params"]["gain_dB"] = 20.0;
    cj["params"]["amp_filepath"] = amp_filepath;
    return cj;
}

// A valid amp-f file: one DEVICE line, one COLUMNS comment, and one block with no VAR or COND.
const char *kMinimalAmpf = "# amp-f v1\nDEVICE TEST\n# COLUMNS: OIP3_dBm\n"
                           "BEGIN\n1000 30.0\n2000 31.0\nEND\n";

void writeAmpf(const std::filesystem::path &path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream ofs(path);
    ofs << kMinimalAmpf;
}

nlohmann::json ampfComponent(const std::string &ampf_filepath) {
    nlohmann::json cj;
    cj["type"] = "Amplifier";
    cj["params"]["gain_dB"] = 20.0;
    cj["params"]["ampf_filepath"] = ampf_filepath;
    return cj;
}

} // namespace

#ifndef TEST_EXTERNAL_PROJECT_DATA_ONLY
TEST_CASE_METHOD(ImGuiFixture,
                 "Project load neutralizes AMP and AMP-F paths outside the project dir",
                 "[containment][project][amp]") {
    auto base = scratchDir("containment_amp_project_test");
    const auto project_path = base / "proj.rfsim";
    const auto decoy = base.parent_path() / "containment_amp_decoy.amp";
    writeAmp(decoy); // valid file OUTSIDE the project dir
    writeAmp(base / "data/good.amp");
    const auto decoy_f = base.parent_path() / "containment_ampf_decoy.ampf";
    writeAmpf(decoy_f); // valid amp-f file OUTSIDE the project dir
    writeAmpf(base / "data/good.ampf");

    nlohmann::json comps = nlohmann::json::array();
    comps.push_back(ampComponent(decoy.string()));       // absolute, outside project dir
    comps.push_back(ampComponent("../../escape.amp"));   // '..' traversal
    comps.push_back(ampComponent("data/good.amp"));      // contained relative path
    comps.push_back(ampfComponent(decoy_f.string()));    // amp-f: absolute, outside project
    comps.push_back(ampfComponent("../../escape.ampf")); // amp-f: '..' traversal
    comps.push_back(ampfComponent("data/good.ampf"));    // amp-f: contained relative path
    comps.push_back(ampfComponent("data/missing.ampf")); // amp-f: contained, but absent
    writeProject(project_path, comps);

    {
        RfSimulatorApp app;
        app.loadProject(project_path.string());
        REQUIRE(app.componentCount() == 7);

        auto amps = app.testComponents().byType<AmplifierEngine>();
        REQUIRE(amps.size() == 7);

        // Absolute path outside the project dir: neutralized, nothing loaded.
        CHECK_FALSE(amps[0]->ampLoaded());
        CHECK(amps[0]->ampFilepath().empty());

        // '..' traversal: neutralized, nothing loaded.
        CHECK_FALSE(amps[1]->ampLoaded());
        CHECK(amps[1]->ampFilepath().empty());

        // Contained relative path: resolved against the project dir (not the CWD) and loaded.
        CHECK(amps[2]->ampLoaded());
        const auto expected = std::filesystem::weakly_canonical(base / "data/good.amp").string();
        CHECK(amps[2]->ampFilepath() == expected);

        // amp-f: the same boundary applies to ampf_filepath.
        CHECK_FALSE(amps[3]->ampfLoaded());
        CHECK(amps[3]->ampfFilepath().empty());
        CHECK_FALSE(amps[4]->ampfLoaded());
        CHECK(amps[4]->ampfFilepath().empty());
        CHECK(amps[5]->ampfLoaded());
        const auto expected_f = std::filesystem::weakly_canonical(base / "data/good.ampf").string();
        CHECK(amps[5]->ampfFilepath() == expected_f);

        // A missing file keeps its path and error, and nothing is loaded.
        CHECK_FALSE(amps[6]->ampfLoaded());
        CHECK(amps[6]->ampfFilepath().find("missing.ampf") != std::string::npos);
        CHECK_FALSE(amps[6]->ampfError().empty());
    }
    std::filesystem::remove_all(base);
    std::filesystem::remove(decoy);
    std::filesystem::remove(decoy_f);
}

TEST_CASE_METHOD(ImGuiFixture, "Project save persists in-project AMP and AMP-F paths as relative",
                 "[containment][project][amp]") {
    auto base = scratchDir("containment_amp_roundtrip_test");
    const auto project_path = base / "proj.rfsim";
    writeAmp(base / "data/good.amp");
    {
        std::ofstream ofs(base / "data/good.ampf");
        ofs << "# amp-f v1\nDEVICE TEST\n# COLUMNS: OIP3_dBm\n"
               "VAR VG=2\nBEGIN\n1000 30.0\n2000 31.0\nEND\n"
               "VAR VG=3\nBEGIN\n1000 40.0\n2000 41.0\nEND\n";
    }

    nlohmann::json comps = nlohmann::json::array();
    comps.push_back(
        ampComponent(std::filesystem::weakly_canonical(base / "data/good.amp").string()));
    comps.push_back(
        ampfComponent(std::filesystem::weakly_canonical(base / "data/good.ampf").string()));
    comps[1]["params"]["ampf_block"]["VG"] = 3.0; // select VG=3 before the save
    writeProject(project_path, comps);

    {
        RfSimulatorApp app;
        app.loadProject(project_path.string());
        auto amps = app.testComponents().byType<AmplifierEngine>();
        REQUIRE(amps.size() == 2);
        REQUIRE(amps[0]->ampLoaded());
        REQUIRE(amps[1]->ampfLoaded());
        REQUIRE(amps[1]->ampfSelection().count("VG") == 1);
        REQUIRE(amps[1]->ampfBlockSelected());
        CHECK(amps[1]->ampfSelection().at("VG") == Catch::Approx(3.0));

        // Re-saving writes the path relative to the project dir, so the file stays portable.
        app.saveProject(project_path.string());
    }

    nlohmann::json saved;
    {
        std::ifstream ifs(project_path);
        ifs >> saved;
    }
    REQUIRE(saved["components"][0]["params"]["amp_filepath"] == "data/good.amp");
    REQUIRE(saved["components"][1]["params"]["ampf_filepath"] == "data/good.ampf");
    REQUIRE(saved["components"][1]["params"]["ampf_block"]["VG"] == 3.0);

    {
        RfSimulatorApp app;
        app.loadProject(project_path.string());
        auto amps = app.testComponents().byType<AmplifierEngine>();
        REQUIRE(amps.size() == 2);
        CHECK(amps[0]->ampLoaded());
        CHECK(amps[1]->ampfLoaded());
        REQUIRE(amps[1]->ampfBlockSelected());
        const auto chosen = selectAmpfBlock(*amps[1]->ampfData(), amps[1]->ampfSelection());
        REQUIRE(chosen.block != nullptr);
        REQUIRE(chosen.block->identity.count("VG") == 1);
        CHECK(chosen.block->identity.at("VG") == Catch::Approx(3.0));
    }
    std::filesystem::remove_all(base);
}
#endif // !TEST_EXTERNAL_PROJECT_DATA_ONLY
#ifdef TEST_EXTERNAL_PROJECT_DATA_ONLY
TEST_CASE_METHOD(ImGuiFixture, "Project save copies an external S-param file and reloads its model",
                 "[containment][project][external]") {
    const auto tag = test_temp_paths::processTag();
    const auto base = scratchDir("containment_external_sparam_" + tag);
    const auto external = base.parent_path() / ("external_" + tag + ".s2p");
    const auto project_path = base / "project.rfsim";
    writeS2p(external);

    {
        RfSimulatorApp app;
        app.newProject();
        auto *amp = dynamic_cast<AmplifierEngine *>(app.testCreateComponent("amplifier", 9901));
        REQUIRE(amp != nullptr);
        amp->setSParamFilepath(external.string());
        amp->setSParamMode(true);
        REQUIRE(amp->sparamLoaded());
        REQUIRE(app.saveProject(project_path.string()));
        CHECK(app.lastSavedDataFiles().size() == 1);
        const auto first_save = nlohmann::json::parse(readFile(project_path));
        const auto first_relative =
            first_save["components"][0]["params"].value("sparam_filepath", "");
        REQUIRE_FALSE(first_relative.empty());
        const auto copied_before_retry = base / first_relative;
        std::filesystem::remove(copied_before_retry);
        auto stale_temp = copied_before_retry;
        stale_temp += ".tmp";
        {
            std::ofstream out(stale_temp, std::ios::binary);
            out << "stale staging file";
        }
        REQUIRE(app.saveProject(project_path.string()));
        CHECK(app.lastSavedDataFiles().size() == 1);
        CHECK(readFile(stale_temp) == "stale staging file");
        REQUIRE(app.loadProject(project_path.string()));
        CHECK(app.testComponents().byType<AmplifierEngine>().front()->sparamLoaded());
    }
    const auto saved = nlohmann::json::parse(readFile(project_path));
    const auto relative = saved["components"][0]["params"].value("sparam_filepath", "");
    REQUIRE_FALSE(relative.empty());
    const auto copied = base / relative;
    CHECK(std::filesystem::exists(copied));
    CHECK(readFile(copied) == readFile(external));
    std::filesystem::remove_all(base);
    std::filesystem::remove(external);
}

TEST_CASE_METHOD(ImGuiFixture, "Project save copies external AMP and AMP-F files and reloads them",
                 "[containment][project][external][amp]") {
    const auto tag = test_temp_paths::processTag();
    const auto base = scratchDir("containment_external_amp_" + tag);
    const auto external = base.parent_path() / ("external_" + tag + ".amp");
    const auto external_f = base.parent_path() / ("external_" + tag + ".ampf");
    const auto project_path = base / "project.rfsim";
    writeAmp(external);
    writeAmpf(external_f);

    {
        RfSimulatorApp app;
        app.newProject();
        auto *amp = dynamic_cast<AmplifierEngine *>(app.testCreateComponent("amplifier", 9902));
        REQUIRE(amp != nullptr);
        amp->setAmpFilepath(external.string());
        REQUIRE(amp->ampLoaded());
        auto *ampf = dynamic_cast<AmplifierEngine *>(app.testCreateComponent("amplifier", 9903));
        REQUIRE(ampf != nullptr);
        ampf->setAmpfFilepath(external_f.string());
        REQUIRE(ampf->ampfLoaded());
        REQUIRE(app.saveProject(project_path.string()));
        CHECK(app.testComponents().byType<AmplifierEngine>().front()->ampLoaded());
        CHECK(app.lastSavedDataFiles().size() == 2);
        REQUIRE(app.loadProject(project_path.string()));
        CHECK(app.testComponents().byType<AmplifierEngine>().front()->ampLoaded());
        CHECK(app.testComponents().byType<AmplifierEngine>().back()->ampfLoaded());
    }
    const auto saved = nlohmann::json::parse(readFile(project_path));
    const auto relative = saved["components"][0]["params"].value("amp_filepath", "");
    REQUIRE_FALSE(relative.empty());
    const auto copied = base / relative;
    CHECK(std::filesystem::exists(copied));
    CHECK(readFile(copied) == readFile(external));
    const auto relative_f = saved["components"][1]["params"].value("ampf_filepath", "");
    REQUIRE_FALSE(relative_f.empty());
    const auto copied_f = base / relative_f;
    CHECK(std::filesystem::exists(copied_f));
    CHECK(readFile(copied_f) == readFile(external_f));
    std::filesystem::remove_all(base);
    std::filesystem::remove(external);
    std::filesystem::remove(external_f);
}

TEST_CASE_METHOD(ImGuiFixture, "Failed external data copy preserves the previous project file",
                 "[containment][project][external][failure]") {
    const auto tag = test_temp_paths::processTag();
    const auto base = scratchDir("containment_external_failure_" + tag);
    const auto external = base.parent_path() / ("missing_" + tag + ".s2p");
    const auto project_path = base / "project.rfsim";
    writeS2p(external);

    RfSimulatorApp app;
    app.newProject();
    auto *amp = dynamic_cast<AmplifierEngine *>(app.testCreateComponent("amplifier", 9903));
    REQUIRE(amp != nullptr);
    amp->setSParamFilepath(external.string());
    amp->setSParamMode(true);
    REQUIRE(amp->sparamLoaded());
    const std::string previous = R"({"previous":"intact"})";
    {
        std::ofstream out(project_path, std::ios::binary | std::ios::trunc);
        out << previous;
    }
    std::filesystem::remove(external);
    CHECK_FALSE(app.saveProject(project_path.string()));
    CHECK(readFile(project_path) == previous);
    std::filesystem::remove_all(base);
}

TEST_CASE_METHOD(ImGuiFixture, "Project save without external data has no data copies",
                 "[containment][project][external]") {
    const auto tag = test_temp_paths::processTag();
    const auto base = scratchDir("containment_no_external_" + tag);
    const auto project_path = base / "project.rfsim";
    RfSimulatorApp app;
    REQUIRE(app.saveProject(project_path.string()));
    CHECK(app.lastSavedDataFiles().empty());
    std::filesystem::remove_all(base);
}
TEST_CASE_METHOD(ImGuiFixture,
                 "Project save resolves relative live model paths against the engine CWD",
                 "[containment][project][external]") {
    const auto tag = test_temp_paths::processTag();
    const auto base = scratchDir("containment_relative_external_" + tag);
    const auto external = base.parent_path() / ("relative_" + tag + ".s2p");
    const auto project_path = base / "project.rfsim";
    writeS2p(external);
    {
        std::ofstream decoy(base / external.filename(), std::ios::binary);
        decoy << "# GHz S MA R 50\n"
                 "1.0 0.1 0.0 2.0 0.0 0.1 0.0 0.1 0.0\n";
    }

    struct RestoreCwd {
        std::filesystem::path path;
        ~RestoreCwd() {
            std::error_code ec;
            std::filesystem::current_path(path, ec);
        }
    } restore{std::filesystem::current_path()};
    std::filesystem::current_path(base.parent_path());

    RfSimulatorApp app;
    app.newProject();
    auto *amp = dynamic_cast<AmplifierEngine *>(app.testCreateComponent("amplifier", 9904));
    REQUIRE(amp != nullptr);
    amp->setSParamFilepath(external.filename().string());
    REQUIRE(amp->sparamLoaded());
    REQUIRE(app.saveProject(project_path.string()));
    REQUIRE(app.loadProject(project_path.string()));
    CHECK(app.testComponents().byType<AmplifierEngine>().front()->sparamLoaded());

    const auto saved = nlohmann::json::parse(readFile(project_path));
    const auto relative = saved["components"][0]["params"].value("sparam_filepath", "");
    REQUIRE_FALSE(relative.empty());
    const auto copied = base / relative;
    CHECK(readFile(copied) == readFile(external));
    std::filesystem::remove_all(base);
    std::filesystem::remove(external);
}
TEST_CASE_METHOD(ImGuiFixture, "Failed project save rolls back earlier external data copies",
                 "[containment][project][external][failure]") {
    const auto tag = test_temp_paths::processTag();
    const auto base = scratchDir("containment_external_rollback_" + tag);
    const auto external = base.parent_path() / ("rollback_" + tag + ".s2p");
    const auto missing = base.parent_path() / ("missing_" + tag + ".amp");
    const auto project_path = base / "project.rfsim";
    writeS2p(external);

    RfSimulatorApp app;
    app.newProject();
    auto *amp = dynamic_cast<AmplifierEngine *>(app.testCreateComponent("amplifier", 9905));
    REQUIRE(amp != nullptr);
    amp->setSParamFilepath(external.string());
    amp->setAmpFilepath(missing.string());
    REQUIRE(amp->sparamLoaded());
    const std::string previous = R"({"previous":"intact"})";
    {
        std::ofstream out(project_path, std::ios::binary | std::ios::trunc);
        out << previous;
    }

    CHECK_FALSE(app.saveProject(project_path.string()));
    CHECK(readFile(project_path) == previous);
    CHECK_FALSE(std::filesystem::exists(base / "rf_data"));
    std::filesystem::remove_all(base);
    std::filesystem::remove(external);
}

TEST_CASE_METHOD(ImGuiFixture, "Project save refuses to overwrite a changed rf_data destination",
                 "[containment][project][external][collision]") {
    const auto tag = test_temp_paths::processTag();
    const auto base = scratchDir("containment_external_collision_" + tag);
    const auto external = base.parent_path() / ("collision_" + tag + ".s2p");
    const auto project_path = base / "project.rfsim";
    writeS2p(external);

    RfSimulatorApp app;
    app.newProject();
    auto *amp = dynamic_cast<AmplifierEngine *>(app.testCreateComponent("amplifier", 9906));
    REQUIRE(amp != nullptr);
    amp->setSParamFilepath(external.string());
    REQUIRE(amp->sparamLoaded());
    REQUIRE(app.saveProject(project_path.string()));

    const auto saved = nlohmann::json::parse(readFile(project_path));
    const auto relative = saved["components"][0]["params"].value("sparam_filepath", "");
    REQUIRE_FALSE(relative.empty());
    const auto copied = base / relative;
    {
        std::ofstream out(copied, std::ios::binary | std::ios::trunc);
        out << "different bytes";
    }
    const std::string previous = R"({"previous":"intact"})";
    {
        std::ofstream out(project_path, std::ios::binary | std::ios::trunc);
        out << previous;
    }

    CHECK_FALSE(app.saveProject(project_path.string()));
    CHECK(readFile(project_path) == previous);
    CHECK(readFile(copied) == "different bytes");
    std::filesystem::remove_all(base);
    std::filesystem::remove(external);
}
TEST_CASE_METHOD(ImGuiFixture, "Project save rejects symlinked RF data sources and destinations",
                 "[containment][project][external][symlink]") {
    const auto tag = test_temp_paths::processTag();
    const auto base = scratchDir("containment_external_symlink_" + tag);
    const auto external = base.parent_path() / ("symlink_target_" + tag + ".s2p");
    const auto source_link = base.parent_path() / ("symlink_source_" + tag + ".s2p");
    const auto project_path = base / "project.rfsim";
    writeS2p(external);
    std::error_code ec;
    std::filesystem::create_symlink(external, source_link, ec);
    if (ec) {
        std::filesystem::remove_all(base);
        std::filesystem::remove(external);
        SKIP("file symlinks are unavailable: " + ec.message());
    }

    {
        RfSimulatorApp app;
        app.newProject();
        auto *amp = dynamic_cast<AmplifierEngine *>(app.testCreateComponent("amplifier", 9907));
        REQUIRE(amp != nullptr);
        amp->setSParamFilepath(source_link.string());
        REQUIRE(amp->sparamLoaded());
        const std::string previous = R"({"previous":"intact"})";
        {
            std::ofstream out(project_path, std::ios::binary | std::ios::trunc);
            out << previous;
        }
        CHECK_FALSE(app.saveProject(project_path.string()));
        CHECK(readFile(project_path) == previous);
    }

    std::filesystem::path copied;
    {
        RfSimulatorApp app;
        app.newProject();
        auto *amp = dynamic_cast<AmplifierEngine *>(app.testCreateComponent("amplifier", 9908));
        REQUIRE(amp != nullptr);
        amp->setSParamFilepath(external.string());
        REQUIRE(amp->sparamLoaded());
        REQUIRE(app.saveProject(project_path.string()));
        const auto saved = nlohmann::json::parse(readFile(project_path));
        copied = base / saved["components"][0]["params"]["sparam_filepath"].get<std::string>();
    }
    std::filesystem::remove(copied, ec);
    ec.clear();
    std::filesystem::create_symlink(external, copied, ec);
    if (ec) {
        std::filesystem::remove_all(base);
        std::filesystem::remove(source_link);
        std::filesystem::remove(external);
        SKIP("destination file symlinks are unavailable: " + ec.message());
    }
    const std::string previous = R"({"previous":"intact"})";
    {
        std::ofstream out(project_path, std::ios::binary | std::ios::trunc);
        out << previous;
    }
    {
        RfSimulatorApp app;
        app.newProject();
        auto *amp = dynamic_cast<AmplifierEngine *>(app.testCreateComponent("amplifier", 9909));
        REQUIRE(amp != nullptr);
        amp->setSParamFilepath(external.string());
        REQUIRE(amp->sparamLoaded());
        CHECK_FALSE(app.saveProject(project_path.string()));
    }
    CHECK(readFile(project_path) == previous);
    std::filesystem::remove_all(base);
    std::filesystem::remove(source_link);
    std::filesystem::remove(external);
}
TEST_CASE_METHOD(ImGuiFixture, "Project save refuses an oversized external RF data file",
                 "[containment][project][external][size]") {
    const auto tag = test_temp_paths::processTag();
    const auto base = scratchDir("containment_external_oversize_" + tag);
    const auto external = base.parent_path() / ("oversize_" + tag + ".s2p");
    const auto project_path = base / "project.rfsim";
    { std::ofstream out(external, std::ios::binary); }
    std::error_code ec;
    std::filesystem::resize_file(external, 256ULL * 1024 * 1024 + 1, ec);
    REQUIRE_FALSE(ec);

    RfSimulatorApp app;
    app.newProject();
    auto *amp = dynamic_cast<AmplifierEngine *>(app.testCreateComponent("amplifier", 9910));
    REQUIRE(amp != nullptr);
    amp->setSParamFilepath(external.string());
    CHECK_FALSE(amp->sparamLoaded());
    const std::string previous = R"({"previous":"intact"})";
    {
        std::ofstream out(project_path, std::ios::binary | std::ios::trunc);
        out << previous;
    }
    CHECK_FALSE(app.saveProject(project_path.string()));
    CHECK(readFile(project_path) == previous);
    std::filesystem::remove_all(base);
    std::filesystem::remove(external);
}
#endif // TEST_EXTERNAL_PROJECT_DATA_ONLY
