#include "budget_stage_evaluator.h"
#include "component_library.h"
#include "test_temp_paths.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

using Catch::Approx;

namespace {

std::string libraryRoot() { return std::string(PROJECT_SOURCE_DIR) + "/component_data/library"; }

const ComponentDefinition *findPart(const ComponentLibrary &library, const std::string &part) {
    for (const auto *definition : library.all()) {
        if (definition->part_number == part) {
            return definition;
        }
    }
    return nullptr;
}

// Copies the library entry into the self-contained stage snapshot the project stores.
BudgetStage stageFrom(const ComponentDefinition &definition) {
    BudgetStage stage;
    stage.type = definition.type;
    stage.part_number = definition.part_number;
    stage.manufacturer = definition.manufacturer;
    stage.description = definition.description;
    stage.parameters = definition.parameters;
    stage.data_files = definition.data_files;
    stage.source_path = definition.source_path;
    return stage;
}

BudgetStage stageFor(const ComponentLibrary &library, const std::string &part) {
    const auto *definition = findPart(library, part);
    REQUIRE(definition != nullptr);
    return stageFrom(*definition);
}

// Writes files in a per-process directory that is removed when the test ends.
class Scratch {
  public:
    Scratch()
        : m_dir(std::filesystem::temp_directory_path() /
                ("rfsim_budget_evaluator_" + test_temp_paths::processTag() + "_" +
                 std::to_string(++s_count))) {
        std::filesystem::create_directories(m_dir);
    }
    ~Scratch() {
        std::error_code ec;
        std::filesystem::remove_all(m_dir, ec);
    }
    Scratch(const Scratch &) = delete;
    Scratch &operator=(const Scratch &) = delete;

    std::string write(const std::string &name, const std::string &text) const {
        const auto path = m_dir / name;
        std::ofstream out(path, std::ios::binary);
        out << text;
        return path.string();
    }

    const std::filesystem::path &dir() const { return m_dir; }

  private:
    static inline int s_count = 0;
    std::filesystem::path m_dir;
};

// A minimal library entry of `type`; `data_file`, when set, is the stage's single declared
// S-parameter reference.
std::string definitionJson(const std::string &type, const std::string &part_number,
                           const nlohmann::json &parameters, const std::string &data_file = {}) {
    nlohmann::json definition = {
        {"schema_version", 1},           {"type", type},
        {"part_number", part_number},    {"manufacturer", "Test"},
        {"description", "Test " + type}, {"parameters", parameters},
    };
    if (!data_file.empty()) {
        definition["data_files"] =
            nlohmann::json::array({{{"type", "s_parameters"}, {"path", data_file}}});
    }
    return definition.dump(2);
}

// The built-in RC-10-10+ definition with a data file attached; the data-file load is what
// enables S-parameter mode.
std::string attenuatorDefinition(const std::string &data_file,
                                 const std::string &part_number = "TEST-ATT") {
    return definitionJson("attenuator", part_number, {{"attenuation_dB", 0.0}}, data_file);
}

} // namespace

TEST_CASE("Evaluator reads a scalar library amplifier", "[budget][evaluator]") {
    ComponentLibrary library;
    library.scan(libraryRoot());
    BudgetConfig config;
    // AM1143's library entry carries a placeholder S-parameter file. This case covers the
    // scalar path, so the snapshot drops the data file and keeps the datasheet parameters.
    BudgetStage stage = stageFor(library, "AM1143");
    stage.data_files.clear();
    config.stages.push_back(stage);

    BudgetStageEvaluator evaluator;
    auto out = evaluator.evaluate(config, library, {1e9}, -30.0);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].resolved);
    REQUIRE(out[0].metrics.gain_dB[0] == Approx(30.0).margin(1e-3));
    REQUIRE(out[0].metrics.nf_dB[0] == Approx(1.5).margin(1e-3));
    REQUIRE(out[0].metrics.oip3_dBm[0] == Approx(35.0).margin(1e-3));
    REQUIRE(out[0].metrics.p1db_dBm[0] == Approx(20.0).margin(1e-3));
    REQUIRE_FALSE(out[0].metrics.out_of_band[0]);
}

TEST_CASE("Evaluator follows an S-parameter file and marks out-of-band points",
          "[budget][evaluator]") {
    Scratch scratch;
    scratch.write("test.s2p", "# Hz S RI R 50\n"
                              "1e9 0 0 0.5 0 0 0 0 0\n"
                              "2e9 0 0 0.25 0 0 0 0 0\n");
    scratch.write("att.json", attenuatorDefinition("test.s2p"));

    ComponentLibrary library;
    library.scan(scratch.dir().string());
    const auto *definition = findPart(library, "TEST-ATT");
    REQUIRE(definition != nullptr);

    BudgetConfig config;
    config.stages.push_back(stageFrom(*definition));

    BudgetStageEvaluator evaluator;
    auto out = evaluator.evaluate(config, library, {1e9, 2e9, 3e9}, -30.0);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].resolved);
    REQUIRE(out[0].metrics.gain_dB[0] == Approx(-6.0206).margin(1e-3));
    REQUIRE(out[0].metrics.gain_dB[1] == Approx(-12.0412).margin(1e-3));
    // A passive has no OIP3/P1dB model and contributes nothing to those cascades.
    REQUIRE(std::isinf(out[0].metrics.oip3_dBm[0]));
    REQUIRE(std::isinf(out[0].metrics.p1db_dBm[0]));
    REQUIRE(out[0].metrics.out_of_band[2]);
    REQUIRE(std::isnan(out[0].metrics.gain_dB[2]));
}

TEST_CASE("Evaluator marks a missing data file unresolved", "[budget][evaluator]") {
    Scratch scratch;
    scratch.write("att.json", attenuatorDefinition("missing.s2p"));

    ComponentLibrary library;
    library.scan(scratch.dir().string());
    const auto *definition = findPart(library, "TEST-ATT");
    REQUIRE(definition != nullptr);

    BudgetConfig config;
    config.stages.push_back(stageFrom(*definition));

    BudgetStageEvaluator evaluator;
    auto out = evaluator.evaluate(config, library, {1e9}, -30.0);
    REQUIRE(out.size() == 1);
    REQUIRE_FALSE(out[0].resolved);
    REQUIRE_FALSE(out[0].diagnostic.empty());
    REQUIRE(std::isnan(out[0].metrics.gain_dB[0]));
    REQUIRE(std::isnan(out[0].metrics.oip3_dBm[0]));
}

TEST_CASE("Evaluator rejects a stage whose declared data file cannot load", "[budget][evaluator]") {
    Scratch scratch;
    // A declared file that exists but cannot be read as Touchstone must not be replaced by
    // the definition's scalar defaults: the stage has to stay unresolved.
    scratch.write("bad.s2p", "not a touchstone file\n");
    std::filesystem::create_directories(scratch.dir() / "dir.s2p");
    scratch.write("one.s2p", "# Hz S RI R 50\n1e9 0.5 0\n");
    scratch.write("bad.json", attenuatorDefinition("bad.s2p", "BAD-FILE"));
    scratch.write("dir.json", attenuatorDefinition("dir.s2p", "DIR-FILE"));
    scratch.write("one.json", attenuatorDefinition("one.s2p", "ONE-PORT"));

    ComponentLibrary library;
    library.scan(scratch.dir().string());

    BudgetStageEvaluator evaluator;
    for (const auto &[part, file] : std::vector<std::pair<std::string, std::string>>{
             {"BAD-FILE", "bad.s2p"}, {"DIR-FILE", "dir.s2p"}, {"ONE-PORT", "one.s2p"}}) {
        INFO("part " << part);
        const auto *definition = findPart(library, part);
        REQUIRE(definition != nullptr);

        BudgetConfig config;
        config.stages.push_back(stageFrom(*definition));
        auto out = evaluator.evaluate(config, library, {1e9}, -30.0);
        REQUIRE(out.size() == 1);
        REQUIRE_FALSE(out[0].resolved);
        // The diagnostic must name the file, or the user cannot tell which reference failed.
        REQUIRE(out[0].diagnostic.find(file) != std::string::npos);
        REQUIRE(std::isnan(out[0].metrics.gain_dB[0]));
    }
}

TEST_CASE("Evaluator rejects a stage whose type does not load S-parameter files",
          "[budget][evaluator]") {
    Scratch scratch;
    // The switch does not support S-parameter files, so a declared one cannot be honoured;
    // evaluating from the switch's scalar model would hide the broken declaration.
    scratch.write("dummy.s2p", "# Hz S RI R 50\n1e9 0 0 0.5 0 0 0 0 0\n");
    scratch.write("switch.json", definitionJson("rf_switch_spdt", "SW-FILE",
                                                {{"insertion_loss_dB", 0.5}}, "dummy.s2p"));

    ComponentLibrary library;
    library.scan(scratch.dir().string());
    const auto *definition = findPart(library, "SW-FILE");
    REQUIRE(definition != nullptr);

    BudgetConfig config;
    config.stages.push_back(stageFrom(*definition));

    BudgetStageEvaluator evaluator;
    auto out = evaluator.evaluate(config, library, {1e9}, -30.0);
    REQUIRE(out.size() == 1);
    REQUIRE_FALSE(out[0].resolved);
    REQUIRE(out[0].diagnostic.find("does not load S-parameter files") != std::string::npos);
}

TEST_CASE("Evaluator rejects an escaping path parameter", "[budget][evaluator]") {
    Scratch scratch;
    scratch.write("att.json",
                  definitionJson("attenuator", "ESC-PARAM",
                                 {{"attenuation_dB", 0.0}, {"sparam_filepath", "../x.s2p"}}));

    ComponentLibrary library;
    library.scan(scratch.dir().string());
    const auto *definition = findPart(library, "ESC-PARAM");
    REQUIRE(definition != nullptr);

    BudgetConfig config;
    config.stages.push_back(stageFrom(*definition));

    BudgetStageEvaluator evaluator;
    auto out = evaluator.evaluate(config, library, {1e9}, -30.0);
    REQUIRE(out.size() == 1);
    REQUIRE_FALSE(out[0].resolved);
    REQUIRE(out[0].diagnostic.find("outside the library directory") != std::string::npos);
}

TEST_CASE("Evaluator treats an empty path parameter as undeclared", "[budget][evaluator]") {
    Scratch scratch;
    // An empty string is not a reference to a file; it must not turn the stage unresolved.
    scratch.write("att.json", definitionJson("attenuator", "EMPTY-PARAM",
                                             {{"attenuation_dB", 0.0}, {"sparam_filepath", ""}}));

    ComponentLibrary library;
    library.scan(scratch.dir().string());
    const auto *definition = findPart(library, "EMPTY-PARAM");
    REQUIRE(definition != nullptr);

    BudgetConfig config;
    config.stages.push_back(stageFrom(*definition));

    BudgetStageEvaluator evaluator;
    auto out = evaluator.evaluate(config, library, {1e9}, -30.0);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].resolved);
    REQUIRE(out[0].metrics.gain_dB[0] == Approx(0.0).margin(1e-6));
}

TEST_CASE("Evaluator rejects an S-parameter path parameter that did not load",
          "[budget][evaluator]") {
    Scratch scratch;
    // The path stays inside the scratch root, so the resolution report is empty. A missing
    // file leaves S-parameter mode off, and the stage must not fall back to the scalar
    // attenuation.
    scratch.write("att.json", definitionJson("attenuator", "BAD-SPARAM-PARAM",
                                             {{"attenuation_dB", 0.0},
                                              {"sparam_mode", true},
                                              {"sparam_filepath", "bad.s2p"}}));

    ComponentLibrary library;
    library.scan(scratch.dir().string());
    const auto *definition = findPart(library, "BAD-SPARAM-PARAM");
    REQUIRE(definition != nullptr);

    BudgetConfig config;
    config.stages.push_back(stageFrom(*definition));

    BudgetStageEvaluator evaluator;
    auto out = evaluator.evaluate(config, library, {1e9}, -30.0);
    REQUIRE(out.size() == 1);
    REQUIRE_FALSE(out[0].resolved);
    REQUIRE(out[0].diagnostic.find("S-parameter mode") != std::string::npos);
    REQUIRE(std::isnan(out[0].metrics.gain_dB[0]));
}

TEST_CASE("Evaluator rejects an S-parameter path parameter whose file cannot be parsed",
          "[budget][evaluator]") {
    Scratch scratch;
    scratch.write("garbled.s2p", "not a touchstone file\n");
    scratch.write("att.json", definitionJson("attenuator", "GARBLED-SPARAM-PARAM",
                                             {{"attenuation_dB", 0.0},
                                              {"sparam_mode", true},
                                              {"sparam_path", "garbled.s2p"}}));

    ComponentLibrary library;
    library.scan(scratch.dir().string());
    const auto *definition = findPart(library, "GARBLED-SPARAM-PARAM");
    REQUIRE(definition != nullptr);

    BudgetConfig config;
    config.stages.push_back(stageFrom(*definition));

    BudgetStageEvaluator evaluator;
    auto out = evaluator.evaluate(config, library, {1e9}, -30.0);
    REQUIRE(out.size() == 1);
    REQUIRE_FALSE(out[0].resolved);
    REQUIRE(out[0].diagnostic.find("S-parameter mode") != std::string::npos);
}

TEST_CASE("Evaluator accepts an S-parameter path parameter that loaded", "[budget][evaluator]") {
    Scratch scratch;
    scratch.write("good.s2p", "# Hz S RI R 50\n1e9 0 0 0.5 0 0 0 0 0\n");
    scratch.write("att.json", definitionJson("attenuator", "GOOD-SPARAM-PARAM",
                                             {{"attenuation_dB", 0.0},
                                              {"sparam_mode", true},
                                              {"sparam_filepath", "good.s2p"}}));

    ComponentLibrary library;
    library.scan(scratch.dir().string());
    const auto *definition = findPart(library, "GOOD-SPARAM-PARAM");
    REQUIRE(definition != nullptr);

    BudgetConfig config;
    config.stages.push_back(stageFrom(*definition));

    BudgetStageEvaluator evaluator;
    auto out = evaluator.evaluate(config, library, {1e9}, -30.0);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].resolved);
    REQUIRE(out[0].metrics.gain_dB[0] == Approx(-6.0206).margin(1e-3));
}

TEST_CASE("Evaluator rejects an AMP path parameter that did not load", "[budget][evaluator]") {
    Scratch scratch;
    scratch.write("amp.json", definitionJson("amplifier", "BAD-AMP-PARAM",
                                             {{"gain_dB", 20.0}, {"amp_filepath", "bad.amp"}}));

    ComponentLibrary library;
    library.scan(scratch.dir().string());
    const auto *definition = findPart(library, "BAD-AMP-PARAM");
    REQUIRE(definition != nullptr);

    BudgetConfig config;
    config.stages.push_back(stageFrom(*definition));

    BudgetStageEvaluator evaluator;
    auto out = evaluator.evaluate(config, library, {1e9}, -30.0);
    REQUIRE(out.size() == 1);
    REQUIRE_FALSE(out[0].resolved);
    REQUIRE(out[0].diagnostic.find("AMP file") != std::string::npos);
    REQUIRE(std::isnan(out[0].metrics.gain_dB[0]));
}

TEST_CASE("Evaluator rejects an amp-f path parameter without a selected block",
          "[budget][evaluator]") {
    Scratch scratch;
    scratch.write("amp.json", definitionJson("amplifier", "BAD-AMPF-PARAM",
                                             {{"gain_dB", 20.0}, {"ampf_filepath", "bad.ampf"}}));

    ComponentLibrary library;
    library.scan(scratch.dir().string());
    const auto *definition = findPart(library, "BAD-AMPF-PARAM");
    REQUIRE(definition != nullptr);

    BudgetConfig config;
    config.stages.push_back(stageFrom(*definition));

    BudgetStageEvaluator evaluator;
    auto out = evaluator.evaluate(config, library, {1e9}, -30.0);
    REQUIRE(out.size() == 1);
    REQUIRE_FALSE(out[0].resolved);
    REQUIRE(out[0].diagnostic.find("amp-f") != std::string::npos);
    REQUIRE(std::isnan(out[0].metrics.gain_dB[0]));
}

TEST_CASE("Evaluator accepts an amp-f path parameter with one block", "[budget][evaluator]") {
    Scratch scratch;
    scratch.write("one.ampf", "# amp-f v1\nDEVICE TEST\n# COLUMNS: OIP3_dBm\nBEGIN\n"
                              "1000 10.0\nEND\n");
    scratch.write(
        "amp.json",
        definitionJson("amplifier", "GOOD-AMPF-PARAM",
                       {{"gain_dB", 20.0}, {"oip3_dBm", 30.0}, {"ampf_filepath", "one.ampf"}}));

    ComponentLibrary library;
    library.scan(scratch.dir().string());
    const auto *definition = findPart(library, "GOOD-AMPF-PARAM");
    REQUIRE(definition != nullptr);

    BudgetConfig config;
    config.stages.push_back(stageFrom(*definition));

    BudgetStageEvaluator evaluator;
    auto out = evaluator.evaluate(config, library, {1e9}, -30.0);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].resolved);
    REQUIRE(out[0].metrics.gain_dB[0] == Approx(20.0).margin(1e-3));
}

TEST_CASE("Evaluator rejects a loaded S-parameter file that does not enable the mode",
          "[budget][evaluator]") {
    Scratch scratch;
    // A one-port file parses, but it cannot enable S-parameter mode on a two-port stage; the
    // declaration is still not honoured.
    scratch.write("one.s2p", "# Hz S RI R 50\n1e9 0.0 0.0\n");
    scratch.write("att.json", definitionJson("attenuator", "ONE-PORT-PARAM",
                                             {{"attenuation_dB", 0.0},
                                              {"sparam_mode", true},
                                              {"sparam_filepath", "one.s2p"}}));

    ComponentLibrary library;
    library.scan(scratch.dir().string());
    const auto *definition = findPart(library, "ONE-PORT-PARAM");
    REQUIRE(definition != nullptr);

    BudgetConfig config;
    config.stages.push_back(stageFrom(*definition));

    BudgetStageEvaluator evaluator;
    auto out = evaluator.evaluate(config, library, {1e9}, -30.0);
    REQUIRE(out.size() == 1);
    REQUIRE_FALSE(out[0].resolved);
    // The diagnostic must name the declared file so the user can find it.
    REQUIRE(out[0].diagnostic.find("one.s2p") != std::string::npos);
    REQUIRE(std::isnan(out[0].metrics.gain_dB[0]));
}

TEST_CASE("Evaluator accepts the legacy sparam_path key", "[budget][evaluator]") {
    Scratch scratch;
    scratch.write("good.s2p", "# Hz S RI R 50\n1e9 0 0 0.5 0 0 0 0 0\n");
    scratch.write("att.json", definitionJson("attenuator", "LEGACY-SPARAM-PARAM",
                                             {{"attenuation_dB", 0.0},
                                              {"sparam_mode", true},
                                              {"sparam_path", "good.s2p"}}));

    ComponentLibrary library;
    library.scan(scratch.dir().string());
    const auto *definition = findPart(library, "LEGACY-SPARAM-PARAM");
    REQUIRE(definition != nullptr);

    BudgetConfig config;
    config.stages.push_back(stageFrom(*definition));

    BudgetStageEvaluator evaluator;
    auto out = evaluator.evaluate(config, library, {1e9}, -30.0);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].resolved);
    REQUIRE(out[0].metrics.gain_dB[0] == Approx(-6.0206).margin(1e-3));
}

TEST_CASE("Evaluator treats empty AMP and amp-f parameters as undeclared", "[budget][evaluator]") {
    Scratch scratch;
    scratch.write("amp.json",
                  definitionJson("amplifier", "EMPTY-AMP-PARAMS",
                                 {{"gain_dB", 20.0}, {"amp_filepath", ""}, {"ampf_filepath", ""}}));

    ComponentLibrary library;
    library.scan(scratch.dir().string());
    const auto *definition = findPart(library, "EMPTY-AMP-PARAMS");
    REQUIRE(definition != nullptr);

    BudgetConfig config;
    config.stages.push_back(stageFrom(*definition));

    BudgetStageEvaluator evaluator;
    auto out = evaluator.evaluate(config, library, {1e9}, -30.0);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].resolved);
    REQUIRE(out[0].metrics.gain_dB[0] == Approx(20.0).margin(1e-3));
}

TEST_CASE("Evaluator lets a selected amp-f block replace a broken AMP file",
          "[budget][evaluator]") {
    Scratch scratch;
    // The selected block supplies the intercepts, so the unreadable legacy AMP file is not a
    // defect for this stage.
    scratch.write("one.ampf", "# amp-f v1\nDEVICE TEST\n# COLUMNS: OIP3_dBm\nBEGIN\n"
                              "1000 10.0\nEND\n");
    scratch.write("amp.json", definitionJson("amplifier", "AMPF-REPLACES-AMP",
                                             {{"gain_dB", 20.0},
                                              {"amp_filepath", "bad.amp"},
                                              {"ampf_filepath", "one.ampf"}}));

    ComponentLibrary library;
    library.scan(scratch.dir().string());
    const auto *definition = findPart(library, "AMPF-REPLACES-AMP");
    REQUIRE(definition != nullptr);

    BudgetConfig config;
    config.stages.push_back(stageFrom(*definition));

    BudgetStageEvaluator evaluator;
    auto out = evaluator.evaluate(config, library, {1e9}, -30.0);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].resolved);
    REQUIRE(out[0].metrics.gain_dB[0] == Approx(20.0).margin(1e-3));
}

TEST_CASE("Evaluator leaves an out-of-band amplifier point invalid", "[budget][evaluator]") {
    ComponentLibrary library;
    library.scan(libraryRoot());
    // AM1143 carries its S-parameter file: in band the amplifier keeps its scalar 35 dBm
    // OIP3, and outside the file band the edge-held gain and intercept must not be used.
    BudgetConfig config;
    config.stages.push_back(stageFor(library, "AM1143"));

    BudgetStageEvaluator evaluator;
    auto out = evaluator.evaluate(config, library, {1e9, 20e9}, -30.0);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].resolved);
    REQUIRE_FALSE(out[0].metrics.out_of_band[0]);
    REQUIRE(out[0].metrics.oip3_dBm[0] == Approx(35.0).margin(1e-3));
    REQUIRE(out[0].metrics.p1db_dBm[0] == Approx(20.0).margin(1e-3));
    REQUIRE(out[0].metrics.out_of_band[1]);
    REQUIRE(std::isnan(out[0].metrics.gain_dB[1]));
    REQUIRE(std::isnan(out[0].metrics.nf_dB[1]));
    REQUIRE(std::isnan(out[0].metrics.oip3_dBm[1]));
    REQUIRE(std::isnan(out[0].metrics.p1db_dBm[1]));
}

TEST_CASE("Evaluator leaves a non-two-port stage unresolved", "[budget][evaluator]") {
    ComponentLibrary library;
    library.scan(libraryRoot());
    // `generator` is a real engine type with no input pin. The loader now rejects it, but a
    // hand-built config must not make the probe write inputs[0] out of bounds.
    BudgetConfig config;
    BudgetStage stage;
    stage.type = "generator";
    stage.part_number = "GEN1";
    config.stages.push_back(stage);

    BudgetStageEvaluator evaluator;
    auto out = evaluator.evaluate(config, library, {1e9}, -30.0);
    REQUIRE(out.size() == 1);
    REQUIRE_FALSE(out[0].resolved);
    REQUIRE(out[0].diagnostic.find("two-port") != std::string::npos);
    REQUIRE(std::isnan(out[0].metrics.gain_dB[0]));
}

TEST_CASE("Evaluator leaves a multi-input stage unresolved", "[budget][evaluator]") {
    ComponentLibrary library;
    library.scan(libraryRoot());
    // `combiner` is a real registry type with two input pins. The project loader excludes it,
    // but a programmatic config must not leave inputs[1] unset for the probe loop.
    BudgetConfig config;
    BudgetStage stage;
    stage.type = "combiner";
    stage.part_number = "COMB1";
    config.stages.push_back(stage);

    BudgetStageEvaluator evaluator;
    auto out = evaluator.evaluate(config, library, {1e9}, -30.0);
    REQUIRE(out.size() == 1);
    REQUIRE_FALSE(out[0].resolved);
    REQUIRE(out[0].diagnostic.find("two-port") != std::string::npos);
    REQUIRE(std::isnan(out[0].metrics.gain_dB[0]));
}

TEST_CASE("Evaluator invalidates a stage after its parameters change", "[budget][evaluator]") {
    ComponentLibrary library;
    library.scan(libraryRoot());
    BudgetConfig config;
    config.stages.push_back(stageFor(library, "AM1143"));
    config.stages[0].data_files.clear();

    BudgetStageEvaluator evaluator;
    auto first = evaluator.evaluate(config, library, {1e9}, -30.0);
    REQUIRE(first[0].metrics.gain_dB[0] == Approx(30.0).margin(1e-3));

    config.stages[0].parameters["gain_dB"] = 20.0;
    auto second = evaluator.evaluate(config, library, {1e9}, -30.0);
    REQUIRE(second[0].metrics.gain_dB[0] == Approx(20.0).margin(1e-3));
}
