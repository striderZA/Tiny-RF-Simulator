// Standalone Catch2 executable for the S1/S2 security fixes from the
// 2026-08-09 codebase review:
//   S1 — S-param file path containment (project-file boundary in
//        ProjectSerializer::load()/save(), library boundary in
//        ComponentLibrary::instantiate())
//   S2 — touchstone parser file-size guard + in-loop frequency-point cap
//
// Built as its own executable rather than appended to the main `tests` binary
// because this MinGW-w64 toolchain silently drops any TEST_CASE registered
// beyond the ceiling in tests.exe (see the comment above test_component_authoring
// in tests/CMakeLists.txt).
#include "amplifier_engine.h"
#include "app.h"
#include "component_library.h"
#include "component_registry.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "node_graph_engine.h"
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
