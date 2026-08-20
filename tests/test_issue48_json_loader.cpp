// Regression coverage for issue #48: wrong-typed but syntactically valid
// project and component-library JSON must not escape load/discovery
// boundaries, and valid sibling entries must be preserved.
//
// Standalone executable (not part of the main `tests` binary) because the
// MinGW-w64 toolchain silently drops TEST_CASE registrations beyond a ceiling
// in tests.exe; these cases must actually run on every CI platform.
#include "amplifier_engine.h"
#include "app.h"
#include "component_library.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "signal_generator_engine.h"
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

struct TempTree {
    std::filesystem::path root;
    ~TempTree() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

TempTree uniqueTempDirectory(std::string_view stem) {
    static std::atomic<unsigned> sequence = 0;
    TempTree tree{std::filesystem::temp_directory_path() /
                  (std::string(stem) + "_" + std::to_string(++sequence))};
    std::filesystem::create_directories(tree.root);
    return tree;
}

// RAII guard for individual temp files: removes the file on scope exit so an
// assertion failure unwinding the test never leaks issue48_*.json.
struct TempFile {
    std::filesystem::path path;
    operator const std::filesystem::path &() const { return path; }
    std::string string() const { return path.string(); }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

TempFile uniqueTempPath(std::string_view stem) {
    static std::atomic<unsigned> sequence = 0;
    return TempFile{std::filesystem::temp_directory_path() /
                    (std::string(stem) + "_" + std::to_string(++sequence) + ".json")};
}

void writeText(const std::filesystem::path &path, std::string_view contents) {
    std::ofstream out(path);
    REQUIRE(out.good());
    out << contents;
    REQUIRE(out.good());
}

} // namespace

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

TEST_CASE_METHOD(ImGuiFixture, "Project loader skips malformed component and keeps valid siblings",
                 "[issue48][project]") {
    const auto path = uniqueTempPath("issue48_project");
    writeText(path, R"({
        "components": [
            {"type": "SignalGenerator", "params": {}},
            {"type": 42, "params": {}},
            {"type": "Amplifier", "params": {"gain_dB": 12.0}}
        ],
        "links": [{"from": "bad", "to": 1}, {"from": 0, "to": 2}],
        "window_state": {"log": true}
    })");

    RfSimulatorApp app;
    app.loadProject(path.string());

    REQUIRE(app.componentCount() == 2);
    REQUIRE(app.testComponents().byType<SignalGeneratorEngine>().size() == 1);
    REQUIRE(app.testComponents().byType<AmplifierEngine>().size() == 1);
}

TEST_CASE_METHOD(ImGuiFixture,
                 "Project loader rejects wrong-shaped top-level sections without throwing",
                 "[issue48][project]") {
    const auto path = uniqueTempPath("issue48_project_shape");
    writeText(path, R"({"components": 5, "links": {}, "groups": 7})");

    RfSimulatorApp app;
    app.loadProject(path.string());

    REQUIRE(app.componentCount() == 0);
}

TEST_CASE("Component library skips wrong-typed required fields", "[issue48][library]") {
    const auto path = uniqueTempPath("issue48_library_required");
    writeText(path, R"({
        "type": 7,
        "part_number": "BAD-TYPE",
        "parameters": {}
    })");

    ComponentLibrary library;
    library.loadFile(path.string());

    REQUIRE(library.all().empty());
}

TEST_CASE("Component library keeps a definition with malformed optional entries",
          "[issue48][library]") {
    const auto path = uniqueTempPath("issue48_library_optional");
    writeText(path, R"({
        "schema_version": "one",
        "type": "amplifier",
        "part_number": "GOOD-AMP",
        "manufacturer": 99,
        "description": ["wrong"],
        "parameters": {"gain_dB": 15.0},
        "data_files": [
            {"type": "s_parameters", "path": 42},
            "not-an-object",
            {"type": "s_parameters", "path": "good.s2p"}
        ]
    })");

    ComponentLibrary library;
    library.loadFile(path.string());

    const auto definitions = library.all();
    REQUIRE(definitions.size() == 1);
    REQUIRE(definitions.front()->type == "amplifier");
    REQUIRE(definitions.front()->part_number == "GOOD-AMP");
    REQUIRE(definitions.front()->data_files.size() == 1);
    REQUIRE(definitions.front()->data_files.front().path == "good.s2p");
}

TEST_CASE("Component library retains definition when schema_version is out of int range",
          "[issue48][library]") {
    const auto path = uniqueTempPath("issue48_library_schema_version_range");
    writeText(path, R"({
        "schema_version": 2147483648,
        "type": "amplifier",
        "part_number": "RANGE-AMP",
        "parameters": {"gain_dB": 15.0}
    })");

    ComponentLibrary library;
    library.loadFile(path.string());

    const auto definitions = library.all();
    REQUIRE(definitions.size() == 1);
    REQUIRE(definitions.front()->type == "amplifier");
    REQUIRE(definitions.front()->part_number == "RANGE-AMP");
    REQUIRE(definitions.front()->schema_version == 1);
}

TEST_CASE("Component library scan continues after malformed files", "[issue48][library]") {
    const auto tree = uniqueTempDirectory("issue48_scan");
    writeText(tree.root / "bad.json", R"({"type": [], "part_number": 4, "parameters": 9})");
    writeText(tree.root / "good.json", R"({
        "type": "attenuator",
        "part_number": "GOOD-ATT",
        "parameters": {"attenuation_dB": 3.0}
    })");

    ComponentLibrary library;
    library.scan(tree.root.string());

    REQUIRE(library.byType("attenuator").size() == 1);
}
