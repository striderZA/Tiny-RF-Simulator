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

TEST_CASE_METHOD(ImGuiFixture,
                 "Project loader skips malformed probe, network analyzer point, and group entries",
                 "[issue48][project]") {
    const auto path = uniqueTempPath("issue48_project_probe_na_group");
    writeText(path, R"({
        "components": [
            {"type": "SignalGenerator", "params": {}},
            {"type": "Amplifier", "params": {"gain_dB": 12.0}}
        ],
        "probe_pins": [
            {"comp": 0, "port": 0, "is_output": "yes"},
            {"comp": 0, "port": 0, "is_output": true}
        ],
        "network_analyzer": {
            "points": 51,
            "point_a": {"comp": 0, "port": 0, "is_output": 1},
            "point_b": {"comp": 1, "port": 0, "is_output": true}
        },
        "groups": [
            {"name": "BAD-GROUP", "member_components": [0, 1], "collapsed": "yes"},
            {"name": "GOOD-GROUP", "member_components": [0, 1], "collapsed": false}
        ]
    })");

    RfSimulatorApp app;
    app.loadProject(path.string());

    REQUIRE(app.componentCount() == 2);
    const auto gens = app.testComponents().byType<SignalGeneratorEngine>();
    const auto amps = app.testComponents().byType<AmplifierEngine>();
    REQUIRE(gens.size() == 1);
    REQUIRE(amps.size() == 1);

    // Only the well-formed probe survives: a non-boolean is_output used to
    // reach get<bool>() and throw, aborting the whole load instead of
    // skipping the entry.
    const auto &probes = app.testGraphEngine().probePins();
    REQUIRE(probes.size() == 1);
    REQUIRE(probes[0] == gens[0]->outputPinId());

    // Point A (non-boolean is_output) is skipped; Point B still restores.
    auto &na = app.testNetworkAnalyzerEngine();
    REQUIRE(na.points() == 51);
    REQUIRE(na.pointAPin() == -1);
    REQUIRE(na.pointBPin() == amps[0]->outputPinId());

    // Only the well-formed group survives; the non-boolean collapsed is
    // logged and skipped with the entry.
    const auto &groups = app.testGraphEngine().groups();
    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0].name == "GOOD-GROUP");
    REQUIRE(!groups[0].collapsed);
}

TEST_CASE_METHOD(ImGuiFixture, "Project loader rejects fractional and oversized integer fields",
                 "[issue48][project]") {
    const auto path = uniqueTempPath("issue48_project_numeric");
    writeText(path, R"({
        "components": [
            {"type": "SignalGenerator", "params": {}},
            {"type": "Amplifier", "params": {"gain_dB": 12.0}}
        ],
        "links": [
            {"from": 4294967296, "to": 1, "from_port": 0, "to_port": 0},
            {"from": -2147483649, "to": 1, "from_port": 0, "to_port": 0},
            {"from": 0, "to": 1, "from_port": 0, "to_port": 0}
        ],
        "probe_pins": [
            {"comp": 1, "port": 4294967296, "is_output": true},
            {"comp": 0, "port": 0, "is_output": true}
        ],
        "network_analyzer": {
            "points": 12.5
        },
        "groups": [
            {"name": "BAD-GROUP", "member_components": [0, 1, 4294967296], "collapsed": false}
        ]
    })");

    RfSimulatorApp app;
    app.loadProject(path.string());

    REQUIRE(app.componentCount() == 2);
    const auto gens = app.testComponents().byType<SignalGeneratorEngine>();
    REQUIRE(gens.size() == 1);

    // Out-of-int-range integers (unsigned 2^32 and signed below INT_MIN) must
    // not wrap/truncate into a bogus link; only the well-formed link survives.
    REQUIRE(app.testGraphEngine().links().size() == 1);

    // An oversized probe port must not truncate to port 0 (which would probe
    // the amplifier output and duplicate the valid generator probe); only the
    // well-formed probe survives.
    const auto &probes = app.testGraphEngine().probePins();
    REQUIRE(probes.size() == 1);
    REQUIRE(probes[0] == gens[0]->outputPinId());

    // Fractional points must not truncate to 12; the engine's current value
    // (default 201) is kept.
    REQUIRE(app.testNetworkAnalyzerEngine().points() == 201);

    // One out-of-int-range member marks the group malformed, so it is skipped.
    REQUIRE(app.testGraphEngine().numGroups() == 0);
}

TEST_CASE_METHOD(ImGuiFixture, "Project loader rolls back component whose nested params throw",
                 "[issue48][project]") {
    // A component record whose nested 'params' content makes engine
    // deserialize() throw must be removed again: the partially created
    // component must not remain counted in the registry or linked in the
    // graph, and the saved-index mapping must stay in step with the file so
    // later sibling records still resolve.
    const auto path = uniqueTempPath("issue48_project_nested_params");
    writeText(path, R"({
        "components": [
            {"type": "SignalGenerator", "params": {}},
            {"type": "Amplifier", "params": {"gain_dB": "not-a-number"}},
            {"type": "Amplifier", "params": {"gain_dB": 12.0}}
        ],
        "links": [{"from": 0, "to": 2, "from_port": 0, "to_port": 0}],
        "probe_pins": [{"comp": 0, "port": 0, "is_output": true}],
        "window_state": {"log": true}
    })");

    RfSimulatorApp app;
    app.loadProject(path.string());

    // The record whose params threw is rolled back: only the two valid
    // components survive (a leftover would count 3).
    REQUIRE(app.componentCount() == 2);
    REQUIRE(app.testComponents().byType<SignalGeneratorEngine>().size() == 1);
    REQUIRE(app.testComponents().byType<AmplifierEngine>().size() == 1);

    // Saved index 2 (the valid amplifier after the skipped record) must still
    // resolve through the preserved mapping, and the generator probe must hit
    // the generator's output pin.
    REQUIRE(app.testGraphEngine().links().size() == 1);
    const auto gens = app.testComponents().byType<SignalGeneratorEngine>();
    const auto &probes = app.testGraphEngine().probePins();
    REQUIRE(probes.size() == 1);
    REQUIRE(probes[0] == gens[0]->outputPinId());
}

TEST_CASE_METHOD(ImGuiFixture, "Project loader resolves probes through skipped components",
                 "[issue48][project]") {
    // Saved probe indices are component indices in the file, not positions in
    // the compacted registry: a skipped earlier record must not shift a later
    // valid probe onto the wrong component or drop it as out of range.
    const auto path = uniqueTempPath("issue48_project_probe_skip");
    writeText(path, R"({
        "components": [
            {"type": 42, "params": {}},
            {"type": "SignalGenerator", "params": {}},
            {"type": "Amplifier", "params": {"gain_dB": 12.0}}
        ],
        "probe_pins": [
            {"comp": 1, "port": 0, "is_output": true},
            {"comp": 2, "port": 0, "is_output": true}
        ],
        "window_state": {"log": true}
    })");

    RfSimulatorApp app;
    app.loadProject(path.string());

    REQUIRE(app.componentCount() == 2);
    const auto gens = app.testComponents().byType<SignalGeneratorEngine>();
    const auto amps = app.testComponents().byType<AmplifierEngine>();
    REQUIRE(gens.size() == 1);
    REQUIRE(amps.size() == 1);

    // comp 1 (generator) restores to the generator's output pin, not the
    // amplifier's; comp 2 (amplifier) restores instead of being dropped
    // because the compacted registry has only two entries.
    const auto &probes = app.testGraphEngine().probePins();
    REQUIRE(probes.size() == 2);
    REQUIRE(probes[0] == gens[0]->outputPinId());
    REQUIRE(probes[1] == amps[0]->outputPinId());
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
