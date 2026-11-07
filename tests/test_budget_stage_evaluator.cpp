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

// The built-in RC-10-10+ definition with a data file attached; the data-file load is what
// enables S-parameter mode.
std::string attenuatorDefinition(const std::string &data_file) {
    nlohmann::json definition = {
        {"schema_version", 1},
        {"type", "attenuator"},
        {"part_number", "TEST-ATT"},
        {"manufacturer", "Test"},
        {"description", "Test attenuator"},
        {"parameters", {{"attenuation_dB", 0.0}}},
        {"data_files", nlohmann::json::array({{{"type", "s_parameters"}, {"path", data_file}}})},
    };
    return definition.dump(2);
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
