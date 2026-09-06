// Standalone Catch2 executable for issue #79: "Validate component-library
// definitions before instantiation".
//
// Malformed definitions (wrong-typed, out-of-range, or unknown-type
// parameters) must be rejected when the library loads them, so they never
// reach the library browser (insertion UI) or the engine factory. Definitions
// that pass field validation but still fail engine deserialization (engine
// state keys with wrong JSON types) must roll back instead of leaving a
// partially registered component. Path-bearing parameters (sparam_filepath)
// are resolved against the library JSON's directory with the same containment
// discipline as data_files entries.
//
// upsert() and instantiate() recompute validation from type/parameters at the
// boundary, so a direct caller cannot bypass the gate by handing over a
// ComponentDefinition whose cached `issues` were never populated.
//
// Kept out of the main `tests` binary for the MinGW-w64 registration-ceiling
// reason documented in tests/AGENTS.md and tests/CMakeLists.txt.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "amplifier_engine.h"
#include "component_library.h"
#include "component_registry.h"
#include "view_manager.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>

namespace fs = std::filesystem;

namespace {

std::string uniqueTempName(const char *tag) {
    static std::random_device rd;
    std::uniform_int_distribution<int> dis(100000, 999999);
    return std::string("issue79_") + tag + "_" + std::to_string(dis(rd));
}

// RAII scratch directory under the system temp dir.
struct TempDir {
    fs::path root;
    explicit TempDir(const char *tag) : root(fs::temp_directory_path() / uniqueTempName(tag)) {
        fs::create_directories(root);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

void writeText(const fs::path &path, const std::string &content) {
    std::ofstream ofs(path);
    ofs << content;
}

// Minimal valid 2-port Touchstone file (freq + S11, S21, S12, S22 in MA).
const char *kMinimalS2p = "# GHz S MA R 50\n"
                          "1.0 0.5 0.0 2.0 90.0 0.1 180.0 0.3 -45.0\n";

void writeS2p(const fs::path &path) {
    fs::create_directories(path.parent_path());
    std::ofstream ofs(path);
    ofs << kMinimalS2p;
}

void writeLibraryJson(const fs::path &path, const nlohmann::json &def) {
    std::ofstream ofs(path);
    ofs << def.dump(2);
}

} // namespace

// --- Load-boundary rejection (acceptance: invalid definitions never reach
// --- the insertion UI or factory) -------------------------------------------

TEST_CASE("ComponentLibrary loadFile rejects a wrong-typed parameter value", "[issue79][library]") {
    TempDir dir("badparam");
    const auto path = dir.root / "bad.json";
    writeText(path, R"({
        "schema_version": 1,
        "type": "amplifier",
        "part_number": "BAD-PARAM",
        "parameters": { "gain_dB": "very loud" }
    })");

    ComponentLibrary lib;
    lib.loadFile(path.string());

    REQUIRE(lib.all().empty());
}

TEST_CASE("ComponentLibrary loadFile rejects an unknown component type", "[issue79][library]") {
    TempDir dir("badtype");
    const auto path = dir.root / "bad.json";
    writeText(path, R"({
        "schema_version": 1,
        "type": "networkanalyzer",
        "part_number": "NOT-A-TYPE",
        "parameters": {}
    })");

    ComponentLibrary lib;
    lib.loadFile(path.string());

    REQUIRE(lib.all().empty());
}

TEST_CASE("ComponentLibrary scan rejects invalid files but keeps valid siblings",
          "[issue79][library]") {
    TempDir dir("scanmix");
    writeText(dir.root / "bad.json", R"({
        "schema_version": 1,
        "type": "amplifier",
        "part_number": "BAD-PARAM",
        "parameters": { "gain_dB": 9999.0 }
    })");
    writeText(dir.root / "good.json", R"({
        "schema_version": 1,
        "type": "amplifier",
        "part_number": "GOOD-PARAM",
        "parameters": { "gain_dB": 15.0 }
    })");

    ComponentLibrary lib;
    lib.scan(dir.root.string());

    auto amps = lib.byType("amplifier");
    REQUIRE(amps.size() == 1);
    REQUIRE(amps[0]->part_number == "GOOD-PARAM");
}

// --- In-memory entry points share the same gate ------------------------------

TEST_CASE("ComponentLibrary upsert refuses a definition carrying validation issues",
          "[issue79][library]") {
    ComponentLibrary lib;
    ComponentDefinition def;
    def.type = "amplifier";
    def.part_number = "UP-BAD";
    def.parameters = {{"gain_dB", 9999.0}}; // out of range
    def.issues = lib.validate(def.type, def.parameters);
    REQUIRE_FALSE(def.issues.empty());

    lib.upsert(def);

    REQUIRE(lib.all().empty());
}

// A direct caller can hand the boundary a ComponentDefinition whose cached
// `issues` were never populated. Validation must be recomputed from type and
// parameters at the boundary, not inferred from caller-maintained state.

TEST_CASE("ComponentLibrary upsert recomputes validation when cached issues are "
          "empty (direct-caller bypass)",
          "[issue79][library]") {
    ComponentLibrary lib;

    // Out-of-range parameters, but the caller never ran validate().
    ComponentDefinition bad;
    bad.type = "amplifier";
    bad.part_number = "UP-BYPASS";
    bad.parameters = {{"gain_dB", 9999.0}};
    REQUIRE(bad.issues.empty());
    lib.upsert(bad);

    // Unknown type with empty cached issues must not be stored either.
    ComponentDefinition unknown;
    unknown.type = "networkanalyzer";
    unknown.part_number = "UP-NOTATYPE";
    unknown.parameters = nlohmann::json::object();
    REQUIRE(unknown.issues.empty());
    lib.upsert(unknown);

    REQUIRE(lib.all().empty());

    // A hand-built valid definition with empty cached issues still enters.
    ComponentDefinition good;
    good.type = "amplifier";
    good.part_number = "UP-GOOD";
    good.parameters = {{"gain_dB", 15.0}};
    lib.upsert(good);

    auto defs = lib.all();
    REQUIRE(defs.size() == 1);
    REQUIRE(defs[0]->part_number == "UP-GOOD");
}

TEST_CASE("ComponentLibrary instantiate recomputes validation when cached "
          "issues are empty (direct-caller bypass)",
          "[issue79][library]") {
    ComponentLibrary lib;
    ComponentDefinition def;
    def.type = "amplifier";
    def.part_number = "FACTORY-BYPASS";
    def.parameters = {{"gain_dB", 9999.0}}; // out of range
    REQUIRE(def.issues.empty());            // caller never ran validate()

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);

    auto *engine = lib.instantiate(def, 705, registry, graph);

    REQUIRE(engine == nullptr);
    REQUIRE(registry.size() == 0);
    REQUIRE(graph.nodes().empty());
}

// --- Deserialization rollback ------------------------------------------------
// A definition can pass field validation (all descriptor fields well-typed)
// yet still trip an engine deserialize() on an engine-state key the library
// schema does not describe (e.g. "enable_nonlinear" as a string). The failure
// must not terminate the caller or leave a partially registered component.

TEST_CASE("ComponentLibrary instantiate rolls back when engine deserialize throws",
          "[issue79][library]") {
    TempDir dir("rollback");
    const auto path = dir.root / "amp.json";
    writeText(path, R"({
        "type": "amplifier",
        "part_number": "THROW-AMP",
        "parameters": { "gain_dB": 20.0, "enable_nonlinear": "yes" }
    })");

    ComponentLibrary lib;
    lib.loadFile(path.string());
    auto defs = lib.all();
    REQUIRE(defs.size() == 1); // passes descriptor validation: no issues
    REQUIRE(defs[0]->issues.empty());

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);

    IComponentEngine *engine = nullptr;
    REQUIRE_NOTHROW(engine = lib.instantiate(*defs[0], 701, registry, graph));

    REQUIRE(engine == nullptr);
    REQUIRE(registry.size() == 0); // no partially registered component
    REQUIRE(graph.nodes().empty());

    // Insertion state stays consistent: a later valid insert works.
    writeText(dir.root / "att.json", R"({
        "type": "attenuator",
        "part_number": "GOOD-ATT",
        "parameters": { "attenuation_dB": 3.0 }
    })");
    ComponentLibrary lib2;
    lib2.loadFile((dir.root / "att.json").string());
    auto *att = lib2.instantiate(*lib2.all()[0], 702, registry, graph);
    REQUIRE(att != nullptr);
    REQUIRE(registry.size() == 1);
    REQUIRE(graph.nodes().size() == 1);
}

// --- Path-bearing parameters: containment like data_files --------------------

TEST_CASE("Library instantiate neutralizes S-param path parameters that escape "
          "the library dir",
          "[issue79][library][containment]") {
    TempDir dir("escaping");
    const auto decoy = dir.root.parent_path() / "issue79_decoy.s2p";
    writeS2p(decoy); // valid file OUTSIDE the library dir

    nlohmann::json j;
    j["schema_version"] = 2;
    j["type"] = "amplifier";
    j["part_number"] = "ESCAPE-PARAM";
    j["parameters"]["gain_dB"] = 20.0;
    j["parameters"]["nf_dB"] = 1.0;
    j["parameters"]["sparam_mode"] = true;
    j["parameters"]["sparam_filepath"] = decoy.string();
    writeLibraryJson(dir.root / "amp.json", j);

    ComponentLibrary lib;
    lib.loadFile((dir.root / "amp.json").string());
    auto defs = lib.all();
    REQUIRE(defs.size() == 1);

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);
    auto *engine = lib.instantiate(*defs[0], 703, registry, graph);
    REQUIRE(engine != nullptr); // falls back to single-point params

    auto *amp = dynamic_cast<AmplifierEngine *>(engine);
    REQUIRE(amp != nullptr);
    CHECK_FALSE(amp->sparamLoaded()); // the decoy was never read
    CHECK(amp->sparamFilepath().empty());
    CHECK(amp->gain_dB() == Catch::Approx(20.0));
}

TEST_CASE("Library instantiate resolves contained S-param path parameters "
          "against the library dir",
          "[issue79][library][containment]") {
    TempDir dir("contained");
    writeS2p(dir.root / "data/good.s2p");

    nlohmann::json j;
    j["schema_version"] = 2;
    j["type"] = "amplifier";
    j["part_number"] = "CONTAINED-PARAM";
    j["parameters"]["gain_dB"] = 20.0;
    j["parameters"]["sparam_mode"] = true;
    j["parameters"]["sparam_filepath"] = "data/good.s2p";
    writeLibraryJson(dir.root / "amp.json", j);

    ComponentLibrary lib;
    lib.loadFile((dir.root / "amp.json").string());
    auto defs = lib.all();
    REQUIRE(defs.size() == 1);

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);
    auto *engine = lib.instantiate(*defs[0], 704, registry, graph);
    REQUIRE(engine != nullptr);

    auto *amp = dynamic_cast<AmplifierEngine *>(engine);
    REQUIRE(amp != nullptr);
    // Resolved relative to the library JSON's directory (the CWD has no
    // data/good.s2p), then loaded.
    REQUIRE(amp->sparamLoaded());
    CHECK(amp->sparamFilepath() == fs::weakly_canonical(dir.root / "data/good.s2p").string());
}
