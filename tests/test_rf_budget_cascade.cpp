// RF budget routes, cascade math, and per-mode verdicts (Plan 2, Task 7). Synthetic stage
// responses pin the math; real idealized and library stages pin the routes and verdicts.
#include "component_library.h"
#include "nonlinear_model.h"
#include "receiver_requirements.h"
#include "rf_budget.h"
#include "rf_budget_cascade.h"
#include "rf_budget_stage.h"
#include "sweep_response.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using Catch::Approx;

namespace {

constexpr double kMargin = 1e-9;
const double kInf = std::numeric_limits<double>::infinity();
const double kNaN = std::numeric_limits<double>::quiet_NaN();

std::vector<double> grid(double start_Hz, double stop_Hz, int points) {
    return linearSweepFrequencies(start_Hz, stop_Hz, points);
}

RfBudgetSeries series(std::vector<double> values, std::uint16_t reason = 0) {
    RfBudgetSeries result;
    result.values = std::move(values);
    result.reasons.assign(result.values.size(), reason);
    return result;
}

RfBudgetSeries flat(std::size_t count, double value, std::uint16_t reason = 0) {
    return series(std::vector<double>(count, value), reason);
}

RfBudgetStageResponse responseOf(std::string name, RfBudgetSeries gain, RfBudgetSeries nf,
                                 RfBudgetSeries oip3, int active_throw = -1) {
    RfBudgetStageResponse response;
    response.stage = std::move(name);
    response.gain_dB.push_back(std::move(gain));
    response.nf_dB.push_back(std::move(nf));
    response.oip3_dBm = std::move(oip3);
    response.active_throw = active_throw;
    return response;
}

std::vector<std::string> stageNames(const RfBudgetCascade &cascade) {
    std::vector<std::string> names;
    for (const auto &stage : cascade.stages)
        names.push_back(stage.stage);
    return names;
}

std::optional<RfBudgetFeed> fromInput() { return RfBudgetFeed{std::nullopt, 0}; }

std::optional<RfBudgetFeed> fromStage(const std::string &stage, int port = 0) {
    return RfBudgetFeed{stage, port};
}

RfBudgetStage idealStage(const std::string &name, const std::string &type,
                         nlohmann::ordered_json params,
                         std::vector<std::optional<RfBudgetFeed>> inputs) {
    RfBudgetStage stage;
    stage.name = name;
    stage.type = type;
    stage.source = RfBudgetStageSource::Idealized;
    stage.params = params.is_null() ? nlohmann::ordered_json::object() : std::move(params);
    stage.inputs = std::move(inputs);
    return stage;
}

// Amplifier 20 dB / NF 3 dB / OIP3 30 dBm into a 6 dB attenuator, one output.
RfBudget chainBudget() {
    RfBudget budget;
    budget.point_count = 5;
    budget.stages.push_back(idealStage(
        "LNA", "amplifier",
        {{"gain_dB", 20.0}, {"nf_dB", 3.0}, {"enable_nonlinear", true}, {"oip3_dBm", 30.0}},
        {fromInput()}));
    budget.stages.push_back(
        idealStage("ATT", "attenuator", {{"atten_dB", 6.0}}, {fromStage("LNA")}));
    budget.outputs.push_back({"RX", "ATT", 0});
    return budget;
}

ReceiverRequirementsConfig bandConfig() {
    ReceiverRequirementsConfig config;
    config.band_start_Hz = 1.0e9;
    config.band_stop_Hz = 2.0e9;
    config.measurement_conditions.input_level_dBm = -30.0;
    return config;
}

ReceiverRequirementsMode requirementMode(std::uint32_t id, std::string name,
                                         ReceiverRequirementsConfig config) {
    ReceiverRequirementsMode mode;
    mode.id = id;
    mode.name = std::move(name);
    mode.config = std::move(config);
    return mode;
}

ReceiverRequirementsSet requirementSet() {
    ReceiverRequirementsSet set;
    set.modes.push_back(requirementMode(1, "Band 1", bandConfig()));
    set.next_mode_id = 2;
    return set;
}

RfBudgetModeRecord emptyRecord(std::uint32_t mode_id) {
    RfBudgetModeRecord record;
    record.mode_id = mode_id;
    return record;
}

const RfBudgetModeResult &modeById(const RfBudgetEvaluation &evaluation, std::uint32_t id) {
    for (const auto &mode : evaluation.modes) {
        if (mode.mode_id == id)
            return mode;
    }
    static RfBudgetModeResult missing;
    return missing;
}

} // namespace

TEST_CASE("Friis, IIP3, level, and headroom for an amplifier then an attenuator",
          "[rf_budget][cascade]") {
    RfBudgetReasonTable reasons;
    const std::uint16_t linear = reasons.add("Stage 'ATT' is linear.");
    const std::size_t points = 5;
    const RfBudgetStageResponse amplifier =
        responseOf("LNA", flat(points, 20.0), flat(points, 3.0), flat(points, 30.0));
    const RfBudgetStageResponse attenuator =
        responseOf("ATT", flat(points, -6.0), flat(points, 6.0), flat(points, kInf, linear));

    const auto stages =
        cascadeRfBudgetPath({{&amplifier, 0, 0}, {&attenuator, 0, 0}}, -30.0, "Band 1", reasons);
    REQUIRE(stages.size() == 2);
    CHECK(stages[0].stage == "LNA");
    CHECK(stages[0].input_port == 0);
    CHECK(stages[0].output_port == 0);

    const double f1 = std::pow(10.0, 3.0 / 10.0);
    const double f2 = std::pow(10.0, 6.0 / 10.0);
    const double expected_nf = 10.0 * std::log10(f1 + (f2 - 1.0) / 100.0);

    for (std::size_t i = 0; i < points; ++i) {
        CHECK(stages[0].gain_dB.values[i] == Approx(20.0).margin(kMargin));
        CHECK(stages[0].nf_dB.values[i] == Approx(3.0).margin(kMargin));
        CHECK(stages[0].oip3_dBm.values[i] == Approx(30.0).margin(kMargin));
        CHECK(stages[0].cumulative_gain_dB.values[i] == Approx(20.0).margin(kMargin));
        CHECK(stages[0].cumulative_nf_dB.values[i] == Approx(3.0).margin(kMargin));
        CHECK(stages[0].cumulative_iip3_dBm.values[i] == Approx(10.0).margin(kMargin));
        CHECK(stages[0].level_dBm.values[i] == Approx(-10.0).margin(kMargin));

        CHECK(stages[1].cumulative_gain_dB.values[i] == Approx(14.0).margin(kMargin));
        CHECK(stages[1].cumulative_nf_dB.values[i] == Approx(expected_nf).margin(kMargin));
        CHECK(stages[1].cumulative_iip3_dBm.values[i] == Approx(10.0).margin(kMargin));
        CHECK(stages[1].level_dBm.values[i] == Approx(-16.0).margin(kMargin));

        const double expected_headroom =
            NonlinearModel::outputLevelForCompression_dBm(30.0, 1.0) + 10.0;
        CHECK(stages[0].p1db_headroom_dB.values[i] == Approx(expected_headroom).margin(kMargin));
        CHECK(std::isnan(stages[1].p1db_headroom_dB.values[i]));
        CHECK(stages[1].p1db_headroom_dB.reasons[i] == linear);
    }
}

TEST_CASE("two amplifiers combine IIP3 reciprocally", "[rf_budget][cascade]") {
    RfBudgetReasonTable reasons;
    const std::size_t points = 3;
    const RfBudgetStageResponse first =
        responseOf("A1", flat(points, 20.0), flat(points, 2.0), flat(points, 30.0));
    const RfBudgetStageResponse second =
        responseOf("A2", flat(points, 10.0), flat(points, 2.0), flat(points, 20.0));

    const auto stages =
        cascadeRfBudgetPath({{&first, 0, 0}, {&second, 0, 0}}, -30.0, "Band 1", reasons);
    REQUIRE(stages.size() == 2);

    const double first_contribution = 100.0 / 1000.0;
    const double second_contribution = 1000.0 / 100.0;
    const double expected = 10.0 * std::log10(1.0 / (first_contribution + second_contribution));
    for (std::size_t i = 0; i < points; ++i) {
        CHECK(stages[0].cumulative_iip3_dBm.values[i] == Approx(10.0).margin(kMargin));
        CHECK(stages[1].cumulative_iip3_dBm.values[i] == Approx(expected).margin(kMargin));
    }
}

TEST_CASE("NF shares sum to 100 percent", "[rf_budget][cascade]") {
    RfBudgetReasonTable reasons;
    const std::uint16_t linear = reasons.add("Stage 'ATT' is linear.");
    const std::size_t points = 4;
    const RfBudgetStageResponse amplifier =
        responseOf("LNA", flat(points, 20.0), flat(points, 3.0), flat(points, 30.0));
    const RfBudgetStageResponse attenuator =
        responseOf("ATT", flat(points, -6.0), flat(points, 6.0), flat(points, kInf, linear));

    const auto stages =
        cascadeRfBudgetPath({{&amplifier, 0, 0}, {&attenuator, 0, 0}}, -30.0, "Band 1", reasons);
    REQUIRE(stages.size() == 2);
    const double f1 = std::pow(10.0, 3.0 / 10.0);
    const double f2 = std::pow(10.0, 6.0 / 10.0);
    const double total = f1 + (f2 - 1.0) / 100.0;
    for (std::size_t i = 0; i < points; ++i) {
        CHECK(stages[0].nf_share_percent.values[i] ==
              Approx(100.0 * (f1 - 1.0) / (total - 1.0)).margin(kMargin));
        CHECK(stages[1].nf_share_percent.values[i] ==
              Approx(100.0 * ((f2 - 1.0) / 100.0) / (total - 1.0)).margin(kMargin));
        CHECK(stages[0].nf_share_percent.values[i] + stages[1].nf_share_percent.values[i] ==
              Approx(100.0).margin(kMargin));
    }
}

TEST_CASE("a missing gain point empties later cumulative values with its reason",
          "[rf_budget][cascade]") {
    RfBudgetReasonTable reasons;
    const std::uint16_t missing = reasons.add("Stage 'M' gain is missing.");
    const std::size_t points = 2;
    const RfBudgetStageResponse first =
        responseOf("A1", flat(points, 10.0), flat(points, 2.0), flat(points, 30.0));
    const RfBudgetStageResponse middle =
        responseOf("M", series(std::vector<double>(points, kNaN), missing), flat(points, 2.0),
                   flat(points, 30.0));
    const RfBudgetStageResponse last =
        responseOf("A2", flat(points, 10.0), flat(points, 2.0), flat(points, 30.0));

    const auto stages = cascadeRfBudgetPath({{&first, 0, 0}, {&middle, 0, 0}, {&last, 0, 0}}, -30.0,
                                            "Band 1", reasons);
    REQUIRE(stages.size() == 3);

    for (std::size_t i = 0; i < points; ++i) {
        CHECK(stages[0].cumulative_gain_dB.values[i] == Approx(10.0).margin(kMargin));
        CHECK(std::isfinite(stages[0].cumulative_nf_dB.values[i]));

        CHECK(std::isnan(stages[1].cumulative_gain_dB.values[i]));
        CHECK(stages[1].cumulative_gain_dB.reasons[i] == missing);
        CHECK(std::isnan(stages[1].cumulative_iip3_dBm.values[i]));
        CHECK(stages[1].cumulative_iip3_dBm.reasons[i] == missing);
        CHECK(std::isnan(stages[1].level_dBm.values[i]));
        CHECK(stages[1].level_dBm.reasons[i] == missing);
        CHECK(std::isnan(stages[1].p1db_headroom_dB.values[i]));
        CHECK(stages[1].p1db_headroom_dB.reasons[i] == missing);

        CHECK(std::isnan(stages[2].cumulative_gain_dB.values[i]));
        CHECK(stages[2].cumulative_gain_dB.reasons[i] == missing);
        CHECK(std::isnan(stages[2].cumulative_nf_dB.values[i]));
        CHECK(stages[2].cumulative_nf_dB.reasons[i] == missing);
        CHECK(std::isnan(stages[2].cumulative_iip3_dBm.values[i]));
        CHECK(stages[2].cumulative_iip3_dBm.reasons[i] == missing);
        CHECK(std::isnan(stages[2].level_dBm.values[i]));
        CHECK(stages[2].level_dBm.reasons[i] == missing);
        CHECK(std::isnan(stages[2].nf_share_percent.values[i]));
        CHECK(stages[2].nf_share_percent.reasons[i] == missing);
        CHECK(std::isnan(stages[2].p1db_headroom_dB.values[i]));
        CHECK(stages[2].p1db_headroom_dB.reasons[i] == missing);
    }
}

TEST_CASE("the -100 dB floor blanks a readout, not the running path", "[rf_budget][cascade]") {
    RfBudgetReasonTable reasons;
    const std::size_t points = 2;
    // A 110 dB pad takes the chain below the floor; a 20 dB amplifier after it brings it back.
    const RfBudgetStageResponse pad =
        responseOf("PAD", flat(points, -110.0), flat(points, 110.0), flat(points, kInf));
    const RfBudgetStageResponse amp =
        responseOf("AMP", flat(points, 20.0), flat(points, 2.0), flat(points, 30.0));

    auto stages = cascadeRfBudgetPath({{&pad, 0, 0}, {&amp, 0, 0}}, -30.0, "Band 1", reasons);
    floorRfBudgetReadouts(stages, reasons);
    REQUIRE(stages.size() == 2);

    for (std::size_t i = 0; i < points; ++i) {
        CHECK(std::isnan(stages[0].cumulative_gain_dB.values[i]));
        CHECK(reasons.at(stages[0].cumulative_gain_dB.reasons[i]).find("below -100 dB") !=
              std::string::npos);
        CHECK(std::isnan(stages[0].cumulative_nf_dB.values[i]));
        CHECK(std::isnan(stages[0].level_dBm.values[i]));
        CHECK(std::isnan(stages[0].p1db_headroom_dB.values[i]));
        CHECK(stages[0].level_dBm.reasons[i] == stages[0].cumulative_gain_dB.reasons[i]);

        CHECK(stages[1].cumulative_gain_dB.values[i] == Approx(-90.0).margin(kMargin));
        CHECK(stages[1].cumulative_gain_dB.reasons[i] == 0);
        CHECK(stages[1].level_dBm.values[i] == Approx(-120.0).margin(kMargin));
    }
}

TEST_CASE("no transmission stays infinite, never NaN", "[rf_budget][cascade]") {
    RfBudgetReasonTable reasons;
    const std::uint16_t stop = reasons.add("Stage 'BPF' passes no signal at this frequency.");
    const std::uint16_t linear = reasons.add("Stage 'BPF' is linear.");
    const std::size_t points = 2;
    const RfBudgetStageResponse first =
        responseOf("A1", flat(points, 10.0), flat(points, 2.0), flat(points, 30.0));
    const RfBudgetStageResponse filter = responseOf(
        "BPF", flat(points, -kInf, stop), flat(points, kInf, stop), flat(points, kInf, linear));
    const RfBudgetStageResponse last =
        responseOf("A2", flat(points, 10.0), flat(points, 2.0), flat(points, 30.0));

    const auto stages = cascadeRfBudgetPath({{&first, 0, 0}, {&filter, 0, 0}, {&last, 0, 0}}, -30.0,
                                            "Band 1", reasons);
    REQUIRE(stages.size() == 3);

    for (std::size_t i = 0; i < points; ++i) {
        for (std::size_t index : {std::size_t{1}, std::size_t{2}}) {
            CHECK(stages[index].cumulative_gain_dB.values[i] == -kInf);
            CHECK(stages[index].cumulative_gain_dB.reasons[i] == stop);
            CHECK(stages[index].cumulative_nf_dB.values[i] == kInf);
            CHECK(stages[index].cumulative_nf_dB.reasons[i] == stop);
            CHECK(stages[index].level_dBm.values[i] == -kInf);
            CHECK(stages[index].level_dBm.reasons[i] == stop);
            CHECK(stages[index].p1db_headroom_dB.values[i] == kInf);
            CHECK(stages[index].p1db_headroom_dB.reasons[i] == stop);
            CHECK_FALSE(std::isnan(stages[index].cumulative_gain_dB.values[i]));
            CHECK_FALSE(std::isnan(stages[index].cumulative_nf_dB.values[i]));
            CHECK_FALSE(std::isnan(stages[index].level_dBm.values[i]));
            CHECK_FALSE(std::isnan(stages[index].p1db_headroom_dB.values[i]));
        }
    }
}

TEST_CASE("a missing input level affects only level, output power, and headroom",
          "[rf_budget][cascade]") {
    RfBudgetReasonTable reasons;
    const std::uint16_t linear = reasons.add("Stage 'ATT' is linear.");
    const std::size_t points = 3;
    const RfBudgetStageResponse amplifier =
        responseOf("LNA", flat(points, 20.0), flat(points, 3.0), flat(points, 30.0));
    const RfBudgetStageResponse attenuator =
        responseOf("ATT", flat(points, -6.0), flat(points, 6.0), flat(points, kInf, linear));

    const auto stages = cascadeRfBudgetPath({{&amplifier, 0, 0}, {&attenuator, 0, 0}}, std::nullopt,
                                            "Band 1", reasons);
    REQUIRE(stages.size() == 2);
    const std::string reason = "Mode 'Band 1' has no input level.";
    for (std::size_t i = 0; i < points; ++i) {
        CHECK(stages[0].cumulative_gain_dB.values[i] == Approx(20.0).margin(kMargin));
        CHECK(std::isfinite(stages[0].cumulative_nf_dB.values[i]));
        CHECK(std::isfinite(stages[0].cumulative_iip3_dBm.values[i]));
        CHECK(std::isfinite(stages[0].nf_share_percent.values[i]));
        CHECK(std::isnan(stages[0].level_dBm.values[i]));
        CHECK(reasons.at(stages[0].level_dBm.reasons[i]) == reason);
        CHECK(std::isnan(stages[0].p1db_headroom_dB.values[i]));
        CHECK(reasons.at(stages[0].p1db_headroom_dB.reasons[i]) == reason);
        CHECK(std::isnan(stages[1].level_dBm.values[i]));
        CHECK(reasons.at(stages[1].level_dBm.reasons[i]) == reason);
        CHECK(std::isnan(stages[1].p1db_headroom_dB.values[i]));
        CHECK(reasons.at(stages[1].p1db_headroom_dB.reasons[i]) == reason);
    }
}

TEST_CASE("path with no nonlinear stage has no IIP3 verdict", "[rf_budget][cascade]") {
    RfBudgetReasonTable reasons;
    const std::uint16_t linear = reasons.add("Stage 'ATT' is linear.");
    const std::size_t points = 2;
    const RfBudgetStageResponse attenuator =
        responseOf("ATT", flat(points, -6.0), flat(points, 6.0), flat(points, kInf, linear));

    const auto stages = cascadeRfBudgetPath({{&attenuator, 0, 0}}, -30.0, "Band 1", reasons);
    REQUIRE(stages.size() == 1);
    for (std::size_t i = 0; i < points; ++i) {
        CHECK(stages[0].cumulative_iip3_dBm.values[i] == kInf);
        CHECK(reasons.at(stages[0].cumulative_iip3_dBm.reasons[i]) ==
              "No nonlinear stage up to 'ATT'.");
    }

    RfBudget budget;
    budget.point_count = 5;
    budget.stages.push_back(idealStage("ATT", "attenuator", {{"atten_dB", 6.0}}, {fromInput()}));
    budget.outputs.push_back({"RX", "ATT", 0});
    budget.modes.push_back(emptyRecord(1));

    ReceiverRequirementsSet requirements = requirementSet();
    requirements.modes[0].config->iip3_min_dBm = 0.0;
    requirements.modes[0].config->measurement_conditions.iip3 =
        ReceiverIIP3TestSettings{1.0e6, -30.0, -20.0, 5.0};
    ComponentLibrary library;
    const RfBudgetEvaluation evaluation = evaluateRfBudget(budget, requirements, library);
    REQUIRE(evaluation.modes.size() == 1);
    REQUIRE(evaluation.modes[0].cascades.size() == 1);
    const RfBudgetCascade &cascade = evaluation.modes[0].cascades[0];
    REQUIRE_FALSE(cascade.stages.empty());
    CHECK(cascade.verdict.iip3.status == ReceiverRequirementStatus::Incomplete);
    for (std::size_t i = 0; i < cascade.frequencies_Hz.size(); ++i) {
        CHECK(std::isnan(cascade.stages.back().cumulative_iip3_dBm.values[i]));
        CHECK(evaluation.reasons.at(cascade.stages.back().cumulative_iip3_dBm.reasons[i]) ==
              "No nonlinear stage on the path; IIP3 cannot be measured.");
    }
}

TEST_CASE("switch overrides route each mode to its own output", "[rf_budget][cascade]") {
    RfBudget budget;
    budget.point_count = 5;
    budget.stages.push_back(idealStage("SW", "rf_switch_spdt", {}, {fromInput()}));
    budget.stages.push_back(
        idealStage("A1", "amplifier", {{"gain_dB", 10.0}}, {fromStage("SW", 0)}));
    budget.stages.push_back(
        idealStage("A2", "amplifier", {{"gain_dB", 10.0}}, {fromStage("SW", 1)}));
    budget.outputs.push_back({"RX1", "A1", 0});
    budget.outputs.push_back({"RX2", "A2", 0});
    budget.modes.push_back(emptyRecord(1));
    budget.modes.push_back({2, {{"SW", {{"active_throw", 1.0}}}}});

    ReceiverRequirementsSet requirements = requirementSet();
    requirements.modes.push_back(requirementMode(2, "Band 2", bandConfig()));
    requirements.next_mode_id = 3;
    for (auto &mode : requirements.modes)
        mode.config->gain = ReceiverGainLimits{5.0, 15.0};

    ComponentLibrary library;
    const RfBudgetEvaluation evaluation = evaluateRfBudget(budget, requirements, library);
    REQUIRE(evaluation.modes.size() == 2);

    const auto &default_mode = evaluation.modes[0];
    CHECK(default_mode.status == RfBudgetModeStatus::Budgeted);
    REQUIRE(default_mode.cascades.size() == 1);
    CHECK(default_mode.cascades[0].output == "RX1");
    CHECK(stageNames(default_mode.cascades[0]) == std::vector<std::string>{"SW", "A1"});
    REQUIRE(default_mode.unreached.size() == 1);
    CHECK(default_mode.unreached[0].output == "RX2");
    CHECK(default_mode.unreached[0].reason == "Switch 'SW' selects T1 in this mode.");
    CHECK(default_mode.cascades[0].verdict.gain.status == ReceiverRequirementStatus::Pass);

    const auto &thrown = evaluation.modes[1];
    REQUIRE(thrown.cascades.size() == 1);
    CHECK(thrown.cascades[0].output == "RX2");
    CHECK(stageNames(thrown.cascades[0]) == std::vector<std::string>{"SW", "A2"});
    REQUIRE(thrown.unreached.size() == 1);
    CHECK(thrown.unreached[0].output == "RX1");
    CHECK(thrown.unreached[0].reason == "Switch 'SW' selects T2 in this mode.");
}

TEST_CASE("splitter feeds every output", "[rf_budget][cascade]") {
    RfBudget budget;
    budget.point_count = 3;
    budget.stages.push_back(idealStage("SPLIT", "splitter", {}, {fromInput()}));
    budget.stages.push_back(
        idealStage("A1", "amplifier", {{"gain_dB", 10.0}}, {fromStage("SPLIT", 0)}));
    budget.stages.push_back(
        idealStage("A2", "amplifier", {{"gain_dB", 20.0}}, {fromStage("SPLIT", 1)}));
    budget.outputs.push_back({"RX1", "A1", 0});
    budget.outputs.push_back({"RX2", "A2", 0});
    budget.modes.push_back(emptyRecord(1));

    ReceiverRequirementsSet requirements = requirementSet();
    ComponentLibrary library;
    const RfBudgetEvaluation evaluation = evaluateRfBudget(budget, requirements, library);
    REQUIRE(evaluation.modes.size() == 1);
    const auto &mode = evaluation.modes[0];
    REQUIRE(mode.cascades.size() == 2);
    CHECK(mode.unreached.empty());
    CHECK(mode.cascades[0].output == "RX1");
    CHECK(stageNames(mode.cascades[0]) == std::vector<std::string>{"SPLIT", "A1"});
    CHECK(mode.cascades[1].output == "RX2");
    CHECK(stageNames(mode.cascades[1]) == std::vector<std::string>{"SPLIT", "A2"});
}

TEST_CASE("switched bank follows the selected 2:1 input", "[rf_budget][cascade]") {
    RfBudget budget;
    budget.point_count = 3;
    budget.stages.push_back(idealStage("SW", "rf_switch_spdt", {}, {fromInput()}));
    budget.stages.push_back(
        idealStage("F1", "amplifier", {{"gain_dB", 3.0}}, {fromStage("SW", 0)}));
    budget.stages.push_back(
        idealStage("F2", "amplifier", {{"gain_dB", 7.0}}, {fromStage("SW", 1)}));
    budget.stages.push_back(
        idealStage("J", "rf_switch_spdt_2to1", {}, {fromStage("F1", 0), fromStage("F2", 0)}));
    budget.outputs.push_back({"RX", "J", 0});
    budget.modes.push_back(emptyRecord(1));
    budget.modes.push_back({2, {{"SW", {{"active_throw", 1.0}}}, {"J", {{"active_throw", 1.0}}}}});

    ReceiverRequirementsSet requirements = requirementSet();
    requirements.modes.push_back(requirementMode(2, "Band 2", bandConfig()));
    requirements.next_mode_id = 3;

    ComponentLibrary library;
    const RfBudgetEvaluation evaluation = evaluateRfBudget(budget, requirements, library);
    REQUIRE(evaluation.modes.size() == 2);

    REQUIRE(evaluation.modes[0].cascades.size() == 1);
    CHECK(stageNames(evaluation.modes[0].cascades[0]) == std::vector<std::string>{"SW", "F1", "J"});
    REQUIRE(evaluation.modes[1].cascades.size() == 1);
    CHECK(stageNames(evaluation.modes[1].cascades[0]) == std::vector<std::string>{"SW", "F2", "J"});
    CHECK(evaluation.modes[0].unreached.empty());
    CHECK(evaluation.modes[1].unreached.empty());
}

TEST_CASE("a blocked branch does not claim an output that a live branch reaches",
          "[rf_budget][cascade]") {
    // SPLIT's first port feeds J's first input, which J blocks. Its second port feeds the input J
    // selects, so RX is reached through A2 and must not be reported unreached.
    RfBudget budget;
    budget.point_count = 3;
    budget.stages.push_back(idealStage("SPLIT", "splitter", {}, {fromInput()}));
    budget.stages.push_back(
        idealStage("A1", "amplifier", {{"gain_dB", 3.0}}, {fromStage("SPLIT", 0)}));
    budget.stages.push_back(
        idealStage("A2", "amplifier", {{"gain_dB", 7.0}}, {fromStage("SPLIT", 1)}));
    budget.stages.push_back(
        idealStage("J", "rf_switch_spdt_2to1", {}, {fromStage("A1", 0), fromStage("A2", 0)}));
    budget.outputs.push_back({"RX", "J", 0});
    budget.modes.push_back({1, {{"J", {{"active_throw", 1.0}}}}});

    ComponentLibrary library;
    const RfBudgetEvaluation evaluation = evaluateRfBudget(budget, requirementSet(), library);
    REQUIRE(evaluation.modes.size() == 1);
    REQUIRE(evaluation.modes[0].cascades.size() == 1);
    CHECK(stageNames(evaluation.modes[0].cascades[0]) ==
          std::vector<std::string>{"SPLIT", "A2", "J"});
    CHECK(evaluation.modes[0].unreached.empty());
}

TEST_CASE("a budget with no outputs is not evaluated", "[rf_budget][cascade]") {
    RfBudget budget = chainBudget();
    budget.outputs.clear();
    ComponentLibrary library;
    const RfBudgetEvaluation evaluation = evaluateRfBudget(budget, requirementSet(), library);
    CHECK(evaluation.not_evaluated_reason == "The budget has no outputs.");
    CHECK(evaluation.modes.empty());
}

TEST_CASE("mode without a record is not budgeted; a record for a removed mode is orphaned",
          "[rf_budget][cascade]") {
    RfBudget budget = chainBudget();
    budget.modes.push_back(emptyRecord(2));
    budget.modes.push_back(emptyRecord(9));

    ReceiverRequirementsSet requirements = requirementSet();
    requirements.modes.push_back(requirementMode(2, "Band 2", bandConfig()));
    requirements.next_mode_id = 3;

    ComponentLibrary library;
    const RfBudgetEvaluation evaluation = evaluateRfBudget(budget, requirements, library);
    REQUIRE(evaluation.modes.size() == 2);
    CHECK(evaluation.modes[0].status == RfBudgetModeStatus::NotBudgeted);
    CHECK(evaluation.modes[0].cascades.empty());
    CHECK(evaluation.modes[1].status == RfBudgetModeStatus::Budgeted);
    REQUIRE(evaluation.orphaned_mode_ids.size() == 1);
    CHECK(evaluation.orphaned_mode_ids[0] == 9);

    RfBudget unrecorded = chainBudget();
    unrecorded.modes.push_back(emptyRecord(2));
    const RfBudgetEvaluation not_budgeted = evaluateRfBudget(unrecorded, requirements, library);
    REQUIRE(not_budgeted.modes.size() == 2);
    CHECK(not_budgeted.modes[0].status == RfBudgetModeStatus::NotBudgeted);
    CHECK(not_budgeted.modes[0].cascades.empty());
}

TEST_CASE("renamed mode keeps its budget record", "[rf_budget][cascade]") {
    RfBudget budget = chainBudget();
    budget.modes.push_back(emptyRecord(1));

    ReceiverRequirementsSet requirements = requirementSet();
    requirements.modes[0].name = "Band A";

    ComponentLibrary library;
    const RfBudgetEvaluation evaluation = evaluateRfBudget(budget, requirements, library);
    REQUIRE(evaluation.modes.size() == 1);
    CHECK(evaluation.modes[0].status == RfBudgetModeStatus::Budgeted);
    CHECK(evaluation.modes[0].mode_name == "Band A");
    CHECK_FALSE(evaluation.modes[0].cascades.empty());
}

TEST_CASE("verdict passes and fails against the mode limits", "[rf_budget][cascade]") {
    RfBudget budget = chainBudget();
    budget.modes.push_back(emptyRecord(1));
    budget.modes.push_back(emptyRecord(2));

    ReceiverRequirementsSet requirements = requirementSet();
    requirements.modes[0].config->gain = ReceiverGainLimits{10.0, 20.0};
    requirements.modes.push_back(requirementMode(2, "Band 2", bandConfig()));
    requirements.modes[1].config->gain = ReceiverGainLimits{15.0, 20.0};
    requirements.next_mode_id = 3;

    ComponentLibrary library;
    const RfBudgetEvaluation evaluation = evaluateRfBudget(budget, requirements, library);
    REQUIRE(evaluation.modes.size() == 2);
    REQUIRE(evaluation.modes[0].cascades.size() == 1);
    REQUIRE(evaluation.modes[1].cascades.size() == 1);
    CHECK(evaluation.modes[0].cascades[0].verdict.gain.status == ReceiverRequirementStatus::Pass);
    CHECK(evaluation.modes[0].cascades[0].verdict.overall == ReceiverRequirementStatus::Pass);
    CHECK(evaluation.modes[1].cascades[0].verdict.gain.status == ReceiverRequirementStatus::Fail);
    CHECK(evaluation.modes[1].cascades[0].verdict.overall == ReceiverRequirementStatus::Fail);
}

TEST_CASE("BPF stopband inside the band fails gain", "[rf_budget][cascade]") {
    RfBudget budget;
    budget.point_count = 5;
    budget.stages.push_back(idealStage("LNA", "amplifier", {{"gain_dB", 10.0}}, {fromInput()}));
    budget.stages.push_back(idealStage(
        "BPF", "filter", {{"filter_type", 2}, {"fc_low_Hz", 1.2e9}, {"fc_high_Hz", 1.8e9}},
        {fromStage("LNA")}));
    budget.outputs.push_back({"RX", "BPF", 0});
    budget.modes.push_back(emptyRecord(1));

    ReceiverRequirementsSet requirements = requirementSet();
    requirements.modes[0].config->gain = ReceiverGainLimits{10.0, 20.0};

    ComponentLibrary library;
    const RfBudgetEvaluation evaluation = evaluateRfBudget(budget, requirements, library);
    REQUIRE(evaluation.modes.size() == 1);
    REQUIRE(evaluation.modes[0].cascades.size() == 1);
    const auto &verdict = evaluation.modes[0].cascades[0].verdict;
    CHECK(verdict.gain.status == ReceiverRequirementStatus::Fail);
    REQUIRE(verdict.gain.observed_min.has_value());
    CHECK(*verdict.gain.observed_min == -kInf);
    CHECK(verdict.overall == ReceiverRequirementStatus::Fail);
}

TEST_CASE("uncovered points make a passing verdict Incomplete", "[rf_budget][cascade]") {
    ComponentLibrary library;
    library.scan(std::string(PROJECT_SOURCE_DIR) + "/component_data/library");

    RfBudget budget;
    budget.point_count = 21;
    RfBudgetStage lna;
    lna.name = "LNA";
    lna.type = "amplifier";
    lna.source = RfBudgetStageSource::Library;
    lna.part_number = "AM1143";
    lna.inputs = {fromInput()};
    budget.stages.push_back(lna);
    budget.outputs.push_back({"RX", "LNA", 0});
    budget.modes.push_back(emptyRecord(1));

    // 10% past the AM1143 file band (0.1-10 GHz) on each side.
    const double start_Hz = 0.09e9;
    const double stop_Hz = 11.0e9;
    RfBudgetReasonTable reasons;
    RfBudgetStageEvaluator evaluator(library, reasons);
    const RfBudgetStageResponse response = evaluator.evaluate(
        budget.stages[0], nullptr, grid(start_Hz, stop_Hz, budget.point_count), 0);
    REQUIRE(response.stage_reason == 0);
    REQUIRE(response.gain_dB.size() == 1);

    double covered_min = kInf;
    double covered_max = -kInf;
    std::vector<std::size_t> uncovered;
    for (std::size_t i = 0; i < response.gain_dB[0].values.size(); ++i) {
        const double value = response.gain_dB[0].values[i];
        if (std::isnan(value)) {
            uncovered.push_back(i);
            continue;
        }
        covered_min = std::min(covered_min, value);
        covered_max = std::max(covered_max, value);
    }
    REQUIRE_FALSE(uncovered.empty());
    REQUIRE(std::isfinite(covered_min));
    REQUIRE(std::isfinite(covered_max));

    ReceiverRequirementsSet requirements = requirementSet();
    ReceiverRequirementsConfig config = bandConfig();
    config.band_start_Hz = start_Hz;
    config.band_stop_Hz = stop_Hz;
    config.gain = ReceiverGainLimits{covered_min - 1.0, covered_max + 1.0};
    requirements.modes[0].config = config;

    const RfBudgetEvaluation evaluation = evaluateRfBudget(budget, requirements, library);
    REQUIRE(evaluation.modes.size() == 1);
    REQUIRE(evaluation.modes[0].cascades.size() == 1);
    const RfBudgetCascade &cascade = evaluation.modes[0].cascades[0];
    CHECK(cascade.verdict.gain.status == ReceiverRequirementStatus::Incomplete);
    CHECK(cascade.verdict.overall == ReceiverRequirementStatus::Incomplete);
    REQUIRE(cascade.stages.size() == 1);
    for (std::size_t index : uncovered) {
        CHECK(std::isnan(cascade.stages[0].gain_dB.values[index]));
        CHECK(evaluation.reasons.at(cascade.stages[0].gain_dB.reasons[index]) ==
              "Stage 'LNA': S-parameter data does not cover this frequency.");
    }
}

TEST_CASE("a chain below the readout floor still fails its gain minimum", "[rf_budget][cascade]") {
    // A 130 dB pad takes the chain to -110 dB, below the -100 dB readout floor. The readout is
    // blank, but the 10 dB gain minimum is still violated, so the verdict must Fail.
    RfBudget budget = chainBudget();
    budget.stages[1].params["atten_dB"] = 130.0;
    budget.modes.push_back(emptyRecord(1));
    ReceiverRequirementsSet requirements = requirementSet();
    requirements.modes[0].config->gain = ReceiverGainLimits{10.0, 20.0};
    ComponentLibrary library;

    const RfBudgetEvaluation evaluation = evaluateRfBudget(budget, requirements, library);
    REQUIRE(evaluation.modes.size() == 1);
    REQUIRE(evaluation.modes[0].cascades.size() == 1);
    const RfBudgetCascade &cascade = evaluation.modes[0].cascades[0];

    CHECK(cascade.verdict.gain.status == ReceiverRequirementStatus::Fail);
    CHECK(cascade.verdict.overall == ReceiverRequirementStatus::Fail);
    for (std::size_t i = 0; i < cascade.frequencies_Hz.size(); ++i)
        CHECK(std::isnan(cascade.stages.back().cumulative_gain_dB.values[i]));
}

TEST_CASE("a chain below the readout floor still fails its NF and output power limits",
          "[rf_budget][cascade]") {
    // The 130 dB pad makes the NF about 110 dB and the output level about -140 dBm. Both limits
    // judge the unfloored values, so both must Fail rather than read Incomplete.
    RfBudget budget = chainBudget();
    budget.stages[1].params["atten_dB"] = 130.0;
    budget.modes.push_back(emptyRecord(1));
    ReceiverRequirementsSet requirements = requirementSet();
    requirements.modes[0].config->nf_max_dB = 10.0;
    requirements.modes[0].config->output_power = ReceiverOutputPowerLimits{-50.0, 20.0};
    ComponentLibrary library;

    const RfBudgetEvaluation evaluation = evaluateRfBudget(budget, requirements, library);
    REQUIRE(evaluation.modes.size() == 1);
    REQUIRE(evaluation.modes[0].cascades.size() == 1);
    const ReceiverRequirementsEvaluation &verdict = evaluation.modes[0].cascades[0].verdict;

    CHECK(verdict.noise_figure.status == ReceiverRequirementStatus::Fail);
    CHECK(verdict.output_power.status == ReceiverRequirementStatus::Fail);
}

TEST_CASE("mode result lookup returns the evaluated mode or null", "[rf_budget][cascade]") {
    RfBudgetEvaluation evaluation;
    evaluation.modes.push_back(RfBudgetModeResult{});
    evaluation.modes.back().mode_id = 4;
    evaluation.modes.back().mode_name = "Band 1";

    REQUIRE(findRfBudgetModeResult(evaluation, 4) != nullptr);
    CHECK(findRfBudgetModeResult(evaluation, 4)->mode_name == "Band 1");
    CHECK(findRfBudgetModeResult(evaluation, 5) == nullptr);
}

TEST_CASE("SNR verdict is input level minus kTB at the config bandwidth and cumulative NF",
          "[rf_budget][cascade]") {
    RfBudget budget;
    budget.point_count = 3;
    budget.stages.push_back(idealStage(
        "LNA", "amplifier",
        {{"gain_dB", 20.0}, {"nf_dB", 3.0}, {"enable_nonlinear", true}, {"oip3_dBm", 30.0}},
        {fromInput()}));
    budget.outputs.push_back({"RX", "LNA", 0});
    budget.modes.push_back(emptyRecord(1));

    ReceiverRequirementsSet requirements = requirementSet();
    requirements.modes[0].config->snr = ReceiverSNRLimit{80.0, 1.0e6};
    ComponentLibrary library;
    const RfBudgetEvaluation evaluation = evaluateRfBudget(budget, requirements, library);
    REQUIRE(evaluation.modes.size() == 1);
    REQUIRE(evaluation.modes[0].cascades.size() == 1);
    const RfBudgetCascade &cascade = evaluation.modes[0].cascades[0];
    // kTB at 290 K over 1 MHz is -113.98 dBm and the LNA NF is 3 dB. With a -30 dBm input,
    // SNR = -30 - (-110.98) = 80.98 dB at every point; the 20 dB gain cancels.
    CHECK(cascade.verdict.snr.status == ReceiverRequirementStatus::Pass);
    REQUIRE(cascade.verdict.snr.observed_min.has_value());
    CHECK(*cascade.verdict.snr.observed_min == Approx(80.976).margin(0.01));
    CHECK(*cascade.verdict.snr.observed_max == Approx(80.976).margin(0.01));

    requirements.modes[0].config->snr = ReceiverSNRLimit{81.0, 1.0e6};
    const RfBudgetEvaluation strict = evaluateRfBudget(budget, requirements, library);
    CHECK(strict.modes[0].cascades[0].verdict.snr.status == ReceiverRequirementStatus::Fail);
}

TEST_CASE("a missing input level leaves the SNR verdict Incomplete", "[rf_budget][cascade]") {
    RfBudget budget;
    budget.point_count = 3;
    budget.stages.push_back(idealStage(
        "LNA", "amplifier",
        {{"gain_dB", 20.0}, {"nf_dB", 3.0}, {"enable_nonlinear", true}, {"oip3_dBm", 30.0}},
        {fromInput()}));
    budget.outputs.push_back({"RX", "LNA", 0});
    budget.modes.push_back(emptyRecord(1));

    ReceiverRequirementsSet requirements = requirementSet();
    requirements.modes[0].config->snr = ReceiverSNRLimit{80.0, 1.0e6};
    requirements.modes[0].config->measurement_conditions.input_level_dBm.reset();
    ComponentLibrary library;
    const RfBudgetEvaluation evaluation = evaluateRfBudget(budget, requirements, library);
    REQUIRE(evaluation.modes.size() == 1);
    REQUIRE(evaluation.modes[0].cascades.size() == 1);
    CHECK(evaluation.modes[0].cascades[0].verdict.snr.status ==
          ReceiverRequirementStatus::Incomplete);
}
