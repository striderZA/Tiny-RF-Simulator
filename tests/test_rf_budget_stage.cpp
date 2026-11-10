// RF budget stage evaluation (Plan 2, Task 6). Each stage is built as a real engine in a private
// scratch graph and driven with the Network Analyzer's own comb, so the budget cannot drift from
// the component models. The Network Analyzer reference run below pins that equivalence.
#include "amplifier_engine.h"
#include "circuit_runtime.h"
#include "component_library.h"
#include "component_params.h"
#include "component_registry.h"
#include "component_type_registry.h"
#include "graph_editor_actions.h"
#include "measurement_chain_runner.h"
#include "network_analyzer_engine.h"
#include "node_graph_engine.h"
#include "rf_budget.h"
#include "rf_budget_stage.h"
#include "sweep_response.h"
#include "test_temp_paths.h"
#include "view_manager.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using Catch::Approx;

namespace {

constexpr double kMargin = 1e-9;
const double kInf = std::numeric_limits<double>::infinity();
const double kNaN = std::numeric_limits<double>::quiet_NaN();

std::vector<double> grid(double start_Hz, double stop_Hz, int points) {
    return linearSweepFrequencies(start_Hz, stop_Hz, points);
}

std::optional<RfBudgetFeed> fromInput() { return RfBudgetFeed{std::nullopt, 0}; }

RfBudgetStage idealStage(const std::string &name, const std::string &type,
                         nlohmann::ordered_json params = nlohmann::ordered_json::object()) {
    RfBudgetStage stage;
    stage.name = name;
    stage.type = type;
    stage.source = RfBudgetStageSource::Idealized;
    stage.params = std::move(params);
    stage.inputs = {fromInput()};
    return stage;
}

RfBudgetStage libraryStage(const std::string &name, const std::string &type,
                           const std::string &part_number) {
    RfBudgetStage stage;
    stage.name = name;
    stage.type = type;
    stage.source = RfBudgetStageSource::Library;
    stage.part_number = part_number;
    stage.inputs = {fromInput()};
    return stage;
}

std::string join(const std::vector<std::string> &parts) {
    std::string joined;
    for (const auto &part : parts)
        joined += (joined.empty() ? "" : " ") + part;
    return joined;
}

// A per-process library folder, removed when the test ends.
class LibraryDir {
  public:
    LibraryDir()
        : m_root(std::filesystem::temp_directory_path() /
                 ("rfsim_budget_stage_" + test_temp_paths::processTag() + "_" +
                  std::to_string(++s_count))) {
        std::filesystem::create_directories(m_root);
    }
    ~LibraryDir() {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }
    LibraryDir(const LibraryDir &) = delete;
    LibraryDir &operator=(const LibraryDir &) = delete;

    void writeFile(const std::string &name, const std::string &text) const {
        std::ofstream out(m_root / name, std::ios::binary);
        out << text;
    }

    void writeDefinition(const std::string &name, const nlohmann::json &definition) const {
        writeFile(name, definition.dump(2));
    }

    const std::filesystem::path &root() const { return m_root; }

  private:
    static inline int s_count = 0;
    std::filesystem::path m_root;
};

nlohmann::json amplifierDefinition(const std::string &part_number, const nlohmann::json &parameters,
                                   const nlohmann::json &data_files = nlohmann::json::array()) {
    return {{"schema_version", 2},
            {"type", "amplifier"},
            {"part_number", part_number},
            {"manufacturer", "Test"},
            {"description", "budget stage fixture"},
            {"parameters", parameters},
            {"test_conditions", nlohmann::json::object()},
            {"data_files", data_files},
            {"notes", ""}};
}

// The Network Analyzer's scratch clone factory: engines come from ComponentTypeRegistry, as
// RfSimulatorApp::NaScratch does.
class RegistryScratch final : public IMeasurementChainScratch {
  public:
    IComponentEngine *createClone(std::string_view type, int id) override {
        const auto *descriptor = ComponentTypeRegistry::instance().find(type);
        if (!descriptor)
            return nullptr;
        return descriptor->create(m_registry, m_graph, id);
    }

  private:
    NodeGraphEngine m_graph;
    ViewManager m_view;
    ComponentRegistry m_registry{m_graph, m_view};
};

class ChainHost final : public IMeasurementChainHost {
  public:
    ChainHost(std::initializer_list<IComponentEngine *> chain) {
        for (auto *component : chain)
            m_by_node[component->graphNodeId()] = component;
    }

    IComponentEngine *componentForNode(int graph_node_id) const override {
        const auto it = m_by_node.find(graph_node_id);
        return it == m_by_node.end() ? nullptr : it->second;
    }

    std::unique_ptr<IMeasurementChainScratch> beginScratchPass() const override {
        return std::make_unique<RegistryScratch>();
    }

  private:
    std::map<int, IComponentEngine *> m_by_node;
};

const ComponentDefinition *findPart(const ComponentLibrary &library,
                                    const std::string &part_number) {
    for (const ComponentDefinition *definition : library.all()) {
        if (definition && definition->part_number == part_number)
            return definition;
    }
    return nullptr;
}

} // namespace

TEST_CASE("idealized amplifier gives its typed gain, NF, and OIP3", "[rf_budget][stage]") {
    ComponentLibrary library;
    RfBudgetReasonTable reasons;
    RfBudgetStageEvaluator evaluator(library, reasons);

    const RfBudgetStage lna = idealStage(
        "LNA", "amplifier",
        {{"gain_dB", 20.0}, {"nf_dB", 3.0}, {"enable_nonlinear", true}, {"oip3_dBm", 30.0}});
    const auto frequencies = grid(1.0e9, 2.0e9, 5);
    const RfBudgetStageResponse response = evaluator.evaluate(lna, nullptr, frequencies, 0);

    CHECK(response.stage == "LNA");
    CHECK(response.stage_reason == 0);
    CHECK(response.active_throw == -1);
    REQUIRE(response.gain_dB.size() == 1);
    REQUIRE(response.nf_dB.size() == 1);
    REQUIRE(response.oip3_dBm.values.size() == frequencies.size());
    for (std::size_t i = 0; i < frequencies.size(); ++i) {
        CHECK(response.gain_dB[0].values[i] == Approx(20.0).margin(kMargin));
        CHECK(response.nf_dB[0].values[i] == Approx(3.0).margin(kMargin));
        CHECK(response.oip3_dBm.values[i] == Approx(30.0).margin(kMargin));
        CHECK(response.gain_dB[0].reasons[i] == 0);
        CHECK(response.nf_dB[0].reasons[i] == 0);
        CHECK(response.oip3_dBm.reasons[i] == 0);
    }
}

TEST_CASE("attenuator NF equals its loss", "[rf_budget][stage]") {
    ComponentLibrary library;
    RfBudgetReasonTable reasons;
    RfBudgetStageEvaluator evaluator(library, reasons);

    const RfBudgetStage att = idealStage("ATT", "attenuator", {{"atten_dB", 6.0}});
    const auto frequencies = grid(1.0e9, 2.0e9, 5);
    const RfBudgetStageResponse response = evaluator.evaluate(att, nullptr, frequencies, 0);

    CHECK(response.stage_reason == 0);
    REQUIRE(response.gain_dB.size() == 1);
    REQUIRE(response.nf_dB.size() == 1);
    for (std::size_t i = 0; i < frequencies.size(); ++i) {
        CHECK(response.gain_dB[0].values[i] == Approx(-6.0).margin(kMargin));
        CHECK(response.nf_dB[0].values[i] == Approx(6.0).margin(kMargin));
        CHECK(response.oip3_dBm.values[i] == kInf);
        CHECK(reasons.at(response.oip3_dBm.reasons[i]) == "Stage 'ATT' is linear.");
    }
}

TEST_CASE("splitter outputs read -3.0103 dB and NF 3.0103 dB on both ports", "[rf_budget][stage]") {
    ComponentLibrary library;
    RfBudgetReasonTable reasons;
    RfBudgetStageEvaluator evaluator(library, reasons);

    const RfBudgetStage split = idealStage("SPLIT", "splitter");
    const auto frequencies = grid(1.0e9, 1.1e9, 3);
    const RfBudgetStageResponse response = evaluator.evaluate(split, nullptr, frequencies, 0);

    CHECK(response.stage_reason == 0);
    REQUIRE(response.gain_dB.size() == 2);
    REQUIRE(response.nf_dB.size() == 2);
    const double expected_dB = 10.0 * std::log10(0.5);
    for (std::size_t port = 0; port < 2; ++port) {
        for (std::size_t i = 0; i < frequencies.size(); ++i) {
            CHECK(response.gain_dB[port].values[i] == Approx(expected_dB).margin(kMargin));
            CHECK(response.nf_dB[port].values[i] == Approx(-expected_dB).margin(kMargin));
        }
    }
}

TEST_CASE("ideal BPF stopband is no transmission", "[rf_budget][stage]") {
    ComponentLibrary library;
    RfBudgetReasonTable reasons;
    RfBudgetStageEvaluator evaluator(library, reasons);

    const RfBudgetStage bpf = idealStage(
        "BPF", "filter", {{"filter_type", 2}, {"fc_low_Hz", 1.0e9}, {"fc_high_Hz", 2.0e9}});
    const auto frequencies = grid(0.5e9, 2.5e9, 5);
    const RfBudgetStageResponse response = evaluator.evaluate(bpf, nullptr, frequencies, 0);

    REQUIRE(response.stage_reason == 0);
    REQUIRE(response.gain_dB.size() == 1);
    REQUIRE(response.nf_dB.size() == 1);
    CHECK(response.gain_dB[0].values[0] == -kInf);
    CHECK(response.nf_dB[0].values[0] == kInf);
    CHECK(reasons.at(response.gain_dB[0].reasons[0]) ==
          "Stage 'BPF' passes no signal at this frequency.");
    CHECK(reasons.at(response.nf_dB[0].reasons[0]) ==
          "Stage 'BPF' passes no signal at this frequency.");
    CHECK(response.gain_dB[0].values[2] == Approx(0.0).margin(kMargin));
    CHECK(response.nf_dB[0].values[2] == Approx(0.0).margin(kMargin));
    CHECK(response.gain_dB[0].reasons[2] == 0);
}

TEST_CASE("override selects the switch throw", "[rf_budget][stage]") {
    ComponentLibrary library;
    RfBudgetReasonTable reasons;
    RfBudgetStageEvaluator evaluator(library, reasons);

    const RfBudgetStage sw = idealStage("SW", "rf_switch_spdt");
    const std::map<std::string, double> overrides{{"active_throw", 1.0}};
    const auto frequencies = grid(1.0e9, 1.1e9, 3);
    const RfBudgetStageResponse response = evaluator.evaluate(sw, &overrides, frequencies, 0);

    CHECK(response.stage_reason == 0);
    CHECK(response.active_throw == 1);
    REQUIRE(response.gain_dB.size() == 2);
    for (std::size_t i = 0; i < frequencies.size(); ++i) {
        CHECK(response.gain_dB[1].values[i] == Approx(-0.5).margin(kMargin));
        CHECK(response.gain_dB[0].values[i] == Approx(-40.0).margin(kMargin));
    }
}

TEST_CASE("library S-parameter part matches the Network Analyzer point for point",
          "[rf_budget][stage]") {
    ComponentLibrary library;
    library.scan(std::string(PROJECT_SOURCE_DIR) + "/component_data/library");
    const ComponentDefinition *definition = findPart(library, "AM1143");
    REQUIRE(definition);

    // 10% past the AM1143 file band (0.1-10 GHz) on each side.
    const auto frequencies = grid(0.09e9, 11.0e9, 21);

    // Reference: a Network Analyzer sweep from a generator through the instantiated part.
    CircuitRuntime runtime;
    GraphEditorActions editor_actions(runtime);
    const auto *generator_descriptor = ComponentTypeRegistry::instance().find("generator");
    REQUIRE(generator_descriptor);
    IComponentEngine *generator = runtime.createComponent(generator_descriptor->create);
    REQUIRE(generator);
    IComponentEngine *amp = library.instantiate(*definition, runtime, editor_actions);
    REQUIRE(amp);
    auto *amplifier = dynamic_cast<AmplifierEngine *>(amp);
    REQUIRE(amplifier);
    // Compression at -30 dBm is about 0.003 dB and would break the tolerance.
    amplifier->setEnableNonlinear(false);
    REQUIRE(runtime.connect(generator->outputPinId(), amp->inputPinId()).has_value());

    ChainHost host({generator, amp});
    NetworkAnalyzerEngine analyzer(runtime.graph(), host);
    analyzer.setStartFrequency(frequencies.front());
    analyzer.setStopFrequency(frequencies.back());
    analyzer.setPoints(static_cast<int>(frequencies.size()));
    analyzer.setStimulusPower(-30.0);
    analyzer.setPointA(generator->outputPinId());
    analyzer.setPointB(amp->outputPinId());
    analyzer.update();
    REQUIRE(analyzer.sweepFrequencies() == frequencies);
    REQUIRE(analyzer.gainDb().size() == frequencies.size());

    RfBudgetReasonTable reasons;
    RfBudgetStageEvaluator evaluator(library, reasons);
    const RfBudgetStageResponse response =
        evaluator.evaluate(libraryStage("LNA", "amplifier", "AM1143"), nullptr, frequencies, 0);
    CHECK(response.stage_reason == 0);
    REQUIRE(response.gain_dB.size() == 1);
    REQUIRE(response.nf_dB.size() == 1);

    std::size_t covered = 0;
    for (std::size_t i = 0; i < frequencies.size(); ++i) {
        if (std::isnan(analyzer.gainDb()[i])) {
            CHECK(std::isnan(response.gain_dB[0].values[i]));
            CHECK(std::isnan(response.nf_dB[0].values[i]));
            CHECK(std::isnan(analyzer.noiseFigureDb()[i]));
            CHECK(reasons.at(response.gain_dB[0].reasons[i]) ==
                  "Stage 'LNA': S-parameter data does not cover this frequency.");
            continue;
        }
        ++covered;
        CHECK(response.gain_dB[0].values[i] == Approx(analyzer.gainDb()[i]).margin(kMargin));
        CHECK(response.nf_dB[0].values[i] == Approx(analyzer.noiseFigureDb()[i]).margin(kMargin));
        CHECK(response.gain_dB[0].reasons[i] == 0);
        CHECK(response.nf_dB[0].reasons[i] == 0);
    }
    // The grid runs past both band edges, so the comparison covers flagged and unflagged points.
    CHECK(covered + 3 == frequencies.size());
}

TEST_CASE("library part whose file does not load has no values", "[rf_budget][stage]") {
    LibraryDir dir;
    dir.writeDefinition("missing.json",
                        amplifierDefinition("MISS-2", {{"gain_dB", 30.0}, {"nf_dB", 1.5}},
                                            nlohmann::json::array({{{"type", "s_parameters"},
                                                                    {"path", "missing.s2p"}}})));
    ComponentLibrary library;
    library.scan(dir.root().string());
    REQUIRE(findPart(library, "MISS-2"));

    RfBudgetReasonTable reasons;
    RfBudgetStageEvaluator evaluator(library, reasons);
    const auto frequencies = grid(1.0e9, 2.0e9, 5);
    const RfBudgetStageResponse response =
        evaluator.evaluate(libraryStage("MISS", "amplifier", "MISS-2"), nullptr, frequencies, 0);

    REQUIRE(response.stage_reason != 0);
    CHECK(reasons.at(response.stage_reason).find("did not load") != std::string::npos);
    REQUIRE(response.gain_dB.size() == 1);
    for (std::size_t i = 0; i < frequencies.size(); ++i) {
        CHECK(std::isnan(response.gain_dB[0].values[i]));
        CHECK(response.gain_dB[0].reasons[i] == response.stage_reason);
        CHECK(response.gain_dB[0].values[i] != 30.0); // never the scalar fallback
    }
}

TEST_CASE("AMP intercept outside the file band has no OIP3 value", "[rf_budget][stage]") {
    LibraryDir dir;
    dir.writeFile("table.amp", "S RI R 50\nFREQ GHZ\n1.0 0.5 0.0 2.0 0.0 0.0 0.0 0.3 0.0\n"
                               "OIP3 dBm\nFREQ GHZ\n1.0 10.0\n1.3 20.0\n");
    dir.writeDefinition("amp.json",
                        amplifierDefinition("AMP-BAND", {{"gain_dB", 20.0},
                                                         {"nf_dB", 2.0},
                                                         {"enable_nonlinear", true},
                                                         {"amp_filepath", "table.amp"}}));
    ComponentLibrary library;
    library.scan(dir.root().string());
    REQUIRE(findPart(library, "AMP-BAND"));

    RfBudgetReasonTable reasons;
    RfBudgetStageEvaluator evaluator(library, reasons);
    const auto frequencies = grid(1.0e9, 2.0e9, 5);
    const RfBudgetStageResponse response =
        evaluator.evaluate(libraryStage("LNA2", "amplifier", "AMP-BAND"), nullptr, frequencies, 0);

    REQUIRE(response.stage_reason == 0);
    REQUIRE(response.oip3_dBm.values.size() == frequencies.size());
    CHECK(response.oip3_dBm.values[0] == Approx(10.0).margin(kMargin));
    CHECK(response.oip3_dBm.reasons[0] == 0);
    for (std::size_t i = 2; i < frequencies.size(); ++i) {
        CHECK(std::isnan(response.oip3_dBm.values[i]));
        CHECK(reasons.at(response.oip3_dBm.reasons[i]) ==
              "Stage 'LNA2': AMP OIP3 data does not cover this frequency.");
    }
}

TEST_CASE("cached amplifier keeps its OIP3 across modes", "[rf_budget][stage]") {
    ComponentLibrary library;
    RfBudgetReasonTable reasons;
    RfBudgetStageEvaluator evaluator(library, reasons);

    const RfBudgetStage lna = idealStage(
        "LNA", "amplifier",
        {{"gain_dB", 20.0}, {"nf_dB", 2.0}, {"enable_nonlinear", true}, {"oip3_dBm", 25.0}});
    const RfBudgetStageResponse first = evaluator.evaluate(lna, nullptr, grid(1.0e9, 2.0e9, 5), 0);
    const RfBudgetStageResponse second = evaluator.evaluate(lna, nullptr, grid(2.0e9, 3.0e9, 5), 0);

    REQUIRE(first.oip3_dBm.values.size() == 5);
    REQUIRE(second.oip3_dBm.values.size() == 5);
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(std::isfinite(first.oip3_dBm.values[i]));
        CHECK(std::isfinite(second.oip3_dBm.values[i]));
        CHECK(first.oip3_dBm.values[i] == Approx(25.0).margin(kMargin));
        CHECK(second.oip3_dBm.values[i] == Approx(25.0).margin(kMargin));
        CHECK(second.oip3_dBm.reasons[i] == 0);
    }
}

TEST_CASE("refused params and overrides name the key", "[rf_budget][stage]") {
    ComponentLibrary library;
    RfBudgetReasonTable reasons;
    RfBudgetStageEvaluator evaluator(library, reasons);
    const auto frequencies = grid(1.0e9, 1.1e9, 3);

    const RfBudgetStage typo = idealStage("LNA", "amplifier", {{"gian_dB", 20.0}});
    const RfBudgetStageResponse typo_response = evaluator.evaluate(typo, nullptr, frequencies, 0);
    REQUIRE(typo_response.stage_reason != 0);
    CHECK(reasons.at(typo_response.stage_reason) ==
          "Stage 'LNA' params refused: unknown key 'gian_dB'");

    const RfBudgetStage coax = idealStage("COAX", "coax");
    const std::map<std::string, double> overrides{{"preset_index", 0.0}};
    const RfBudgetStageResponse coax_response =
        evaluator.evaluate(coax, &overrides, frequencies, 0);
    REQUIRE(coax_response.stage_reason != 0);
    const std::string &message = reasons.at(coax_response.stage_reason);
    CHECK(message.starts_with("Mode override 'COAX.preset_index' refused: "));
    CHECK(message.find("'preset_index'") != std::string::npos);
}

TEST_CASE("checkRfBudgetParams reports the JSON pointer", "[rf_budget][stage]") {
    RfBudget bad_params;
    bad_params.stages.push_back(idealStage("LNA", "amplifier", {{"gain_dB", 20.0}}));
    bad_params.stages.push_back(idealStage("ATT", "amplifier", {{"gian_dB", 6.0}}));
    bad_params.outputs.push_back({"RX", "ATT", 0});

    const auto params_issue = checkRfBudgetParams(bad_params);
    REQUIRE(params_issue.has_value());
    CHECK(params_issue->path == "/budget/stages/1/params");
    CHECK(params_issue->result.status == ParamWriteStatus::UnknownKey);

    RfBudget bad_override;
    bad_override.stages.push_back(idealStage("ATT", "coax"));
    bad_override.outputs.push_back({"RX", "ATT", 0});
    bad_override.modes.push_back({1, {{"ATT", {{"preset_index", 0.0}}}}});

    const auto override_issue = checkRfBudgetParams(bad_override);
    REQUIRE(override_issue.has_value());
    CHECK(override_issue->path == "/budget/modes/0/overrides/ATT");
    CHECK(override_issue->result.status == ParamWriteStatus::TypeMismatch);
    CHECK(override_issue->result.path == "preset_index");
}
