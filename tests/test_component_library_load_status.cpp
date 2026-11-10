#include "amplifier_engine.h"
#include "circuit_runtime.h"
#include "component_library.h"
#include "component_type_registry.h"
#include "graph_editor_actions.h"
#include "test_temp_paths.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// A per-process library folder, removed when the test ends.
class LibraryDir {
  public:
    LibraryDir()
        : m_root(fs::temp_directory_path() /
                 ("rfsim_library_load_status_" + test_temp_paths::processTag() + "_" +
                  std::to_string(++s_count))) {
        fs::create_directories(m_root);
    }
    ~LibraryDir() {
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }
    LibraryDir(const LibraryDir &) = delete;
    LibraryDir &operator=(const LibraryDir &) = delete;

    const fs::path &root() const { return m_root; }

    void writeFile(const std::string &name, const std::string &text) const {
        std::ofstream out(m_root / name, std::ios::binary);
        out << text;
    }

    void writeDefinition(const std::string &name, const nlohmann::json &definition) const {
        writeFile(name, definition.dump(2));
    }

  private:
    static inline int s_count = 0;
    fs::path m_root;
};

// An amplifier library entry with the given parameters and data files.
nlohmann::json amplifierDefinition(const std::string &part_number, const nlohmann::json &parameters,
                                   const nlohmann::json &data_files) {
    return {{"schema_version", 2},
            {"type", "amplifier"},
            {"part_number", part_number},
            {"manufacturer", "Test"},
            {"description", "load status fixture"},
            {"parameters", parameters},
            {"test_conditions", nlohmann::json::object()},
            {"data_files", data_files},
            {"notes", ""}};
}

nlohmann::json sparamFile(const std::string &path) {
    return nlohmann::json::array({{{"type", "s_parameters"}, {"path", path}}});
}

const ComponentDefinition *findPart(const ComponentLibrary &library,
                                    const std::string &part_number) {
    for (const ComponentDefinition *definition : library.all()) {
        if (definition && definition->part_number == part_number)
            return definition;
    }
    return nullptr;
}

// The path a declared file resolves to: the library folder joined with the name, canonical.
std::string resolvedIn(const LibraryDir &dir, const std::string &name) {
    return fs::weakly_canonical(dir.root() / name).string();
}

// Creates the amplifier engine a library part would get, in a live runtime.
IComponentEngine *createAmplifier(CircuitRuntime &runtime) {
    const auto *descriptor = ComponentTypeRegistry::instance().find("amplifier");
    return descriptor ? runtime.createComponent(descriptor->create) : nullptr;
}

} // namespace

TEST_CASE("a valid two-port s2p gives no sentence and enables S-parameter mode",
          "[library_load_status]") {
    LibraryDir dir;
    dir.writeFile("good.s2p", "# GHZ S RI R 50\n1.0 0.5 0.0 2.0 0.0 0.0 0.0 0.3 0.0\n"
                              "2.0 0.5 0.0 2.0 0.0 0.0 0.0 0.3 0.0\n");
    dir.writeDefinition("good.json",
                        amplifierDefinition("GOOD-2", {{"gain_dB", 10.0}}, sparamFile("good.s2p")));
    ComponentLibrary library;
    library.scan(dir.root().string());
    const ComponentDefinition *def = findPart(library, "GOOD-2");
    REQUIRE(def);

    CircuitRuntime runtime;
    IComponentEngine *engine = createAmplifier(runtime);
    REQUIRE(engine);
    CHECK(library.applyDefinition(*def, *engine).empty());
    CHECK(engine->serialize().value("sparam_mode", false));
}

TEST_CASE("a missing s2p gives the exact did-not-load sentence", "[library_load_status]") {
    LibraryDir dir;
    dir.writeDefinition(
        "miss.json", amplifierDefinition("MISS-2", {{"gain_dB", 10.0}}, sparamFile("missing.s2p")));
    ComponentLibrary library;
    library.scan(dir.root().string());
    const ComponentDefinition *def = findPart(library, "MISS-2");
    REQUIRE(def);

    CircuitRuntime runtime;
    IComponentEngine *engine = createAmplifier(runtime);
    REQUIRE(engine);
    const auto sentences = library.applyDefinition(*def, *engine);
    REQUIRE(sentences.size() == 1);
    CHECK(sentences[0] ==
          "S-parameter file '" + resolvedIn(dir, "missing.s2p") + "' for MISS-2 did not load.");
}

TEST_CASE("a one-port file gives the did-not-load sentence", "[library_load_status]") {
    LibraryDir dir;
    dir.writeFile("one.s1p", "# GHZ S RI R 50\n1.0 0.5 0.0\n2.0 0.5 0.0\n");
    dir.writeDefinition("one.json",
                        amplifierDefinition("ONE-1", {{"gain_dB", 10.0}}, sparamFile("one.s1p")));
    ComponentLibrary library;
    library.scan(dir.root().string());
    const ComponentDefinition *def = findPart(library, "ONE-1");
    REQUIRE(def);

    CircuitRuntime runtime;
    IComponentEngine *engine = createAmplifier(runtime);
    REQUIRE(engine);
    const auto sentences = library.applyDefinition(*def, *engine);
    REQUIRE(sentences.size() == 1);
    CHECK(sentences[0] ==
          "S-parameter file '" + resolvedIn(dir, "one.s1p") + "' for ONE-1 did not load.");
    CHECK_FALSE(engine->serialize().value("sparam_mode", true));
}

TEST_CASE("a data file that escapes the library folder gives the outside sentence",
          "[library_load_status]") {
    LibraryDir dir;
    dir.writeDefinition("out.json",
                        amplifierDefinition("OUT-1", {{"gain_dB", 10.0}}, sparamFile("../x.s2p")));
    ComponentLibrary library;
    library.scan(dir.root().string());
    const ComponentDefinition *def = findPart(library, "OUT-1");
    REQUIRE(def);

    CircuitRuntime runtime;
    IComponentEngine *engine = createAmplifier(runtime);
    REQUIRE(engine);
    const auto sentences = library.applyDefinition(*def, *engine);
    REQUIRE(sentences.size() == 1);
    CHECK(sentences[0] == "Data file '../x.s2p' for OUT-1 is outside the library folder.");
}

TEST_CASE("a data file on a part with no library folder gives the no-folder sentence",
          "[library_load_status]") {
    ComponentDefinition def;
    def.schema_version = 2;
    def.type = "amplifier";
    def.part_number = "NOLIB";
    def.parameters = nlohmann::json::object();
    def.source_path.clear();
    def.data_files = {{"s_parameters", "x.s2p"}};

    CircuitRuntime runtime;
    IComponentEngine *engine = createAmplifier(runtime);
    REQUIRE(engine);
    ComponentLibrary library;
    const auto sentences = library.applyDefinition(def, *engine);
    REQUIRE(sentences.size() == 1);
    CHECK(sentences[0] == "NOLIB has no library folder to resolve 'x.s2p' against.");
}

TEST_CASE("a declared S-parameter path with sparam_mode that does not load gives a sentence",
          "[library_load_status]") {
    LibraryDir dir;
    dir.writeDefinition("decl.json", amplifierDefinition("DECL-1",
                                                         {{"gain_dB", 10.0},
                                                          {"sparam_filepath", "missing.s2p"},
                                                          {"sparam_mode", true}},
                                                         nlohmann::json::array()));
    ComponentLibrary library;
    library.scan(dir.root().string());
    const ComponentDefinition *def = findPart(library, "DECL-1");
    REQUIRE(def);

    CircuitRuntime runtime;
    IComponentEngine *engine = createAmplifier(runtime);
    REQUIRE(engine);
    const auto sentences = library.applyDefinition(*def, *engine);
    REQUIRE(sentences.size() == 1);
    CHECK(sentences[0] ==
          "S-parameter file '" + resolvedIn(dir, "missing.s2p") + "' for DECL-1 did not load.");
}

TEST_CASE("a bad AMP file gives one sentence containing the parser error",
          "[library_load_status]") {
    LibraryDir dir;
    dir.writeFile("bad.amp", "this is not an AMP file\n");
    dir.writeDefinition("bad.json",
                        amplifierDefinition("BADAMP-1",
                                            {{"gain_dB", 10.0}, {"amp_filepath", "bad.amp"}},
                                            nlohmann::json::array()));
    ComponentLibrary library;
    library.scan(dir.root().string());
    const ComponentDefinition *def = findPart(library, "BADAMP-1");
    REQUIRE(def);

    CircuitRuntime runtime;
    IComponentEngine *engine = createAmplifier(runtime);
    REQUIRE(engine);
    const auto sentences = library.applyDefinition(*def, *engine);
    REQUIRE(sentences.size() == 1);
    const auto *amp = dynamic_cast<const AmplifierEngine *>(engine);
    REQUIRE(amp);
    REQUIRE_FALSE(amp->ampError().empty());
    CHECK(sentences[0].find("AMP file '" + resolvedIn(dir, "bad.amp") +
                            "' for BADAMP-1 "
                            "did not load: " +
                            amp->ampError() + ".") == 0);
}

TEST_CASE("an amp-f selection that matches no block gives one sentence", "[library_load_status]") {
    LibraryDir dir;
    dir.writeFile("one.ampf", "# amp-f v1\nDEVICE TEST\n# COLUMNS: OIP3_dBm\nBEGIN\n"
                              "1000 10.0\n1300 10.0\nEND\n");
    dir.writeDefinition("ampf.json", amplifierDefinition("AMPF-1",
                                                         {{"gain_dB", 10.0},
                                                          {"ampf_filepath", "one.ampf"},
                                                          {"ampf_block", {{"TEMP", 25.0}}}},
                                                         nlohmann::json::array()));
    ComponentLibrary library;
    library.scan(dir.root().string());
    const ComponentDefinition *def = findPart(library, "AMPF-1");
    REQUIRE(def);

    CircuitRuntime runtime;
    IComponentEngine *engine = createAmplifier(runtime);
    REQUIRE(engine);
    const auto sentences = library.applyDefinition(*def, *engine);
    REQUIRE(sentences.size() == 1);
    CHECK(sentences[0] == "amp-f file '" + resolvedIn(dir, "one.ampf") +
                              "' for AMPF-1: no amp-f block has the selected VAR and COND "
                              "values.");
}

TEST_CASE("instantiate() of a part with a missing file still returns an engine",
          "[library_load_status]") {
    LibraryDir dir;
    dir.writeDefinition(
        "miss.json", amplifierDefinition("MISS-3", {{"gain_dB", 10.0}}, sparamFile("missing.s2p")));
    ComponentLibrary library;
    library.scan(dir.root().string());
    const ComponentDefinition *def = findPart(library, "MISS-3");
    REQUIRE(def);

    CircuitRuntime runtime;
    GraphEditorActions actions{runtime};
    IComponentEngine *engine = library.instantiate(*def, runtime, actions);
    CHECK(engine != nullptr);
}
