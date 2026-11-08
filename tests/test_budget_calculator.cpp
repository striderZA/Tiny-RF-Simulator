#include "budget_calculator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using Catch::Approx;

TEST_CASE("Budget cascade applies Friis noise", "[budget]") {
    BudgetConfig config;
    config.stages = {BudgetStage{}, BudgetStage{}};
    std::vector<double> freqs{1e9};
    BudgetStageMetrics a{{10.0}, {2.0}, {30.0}, {20.0}, {false}};
    BudgetStageMetrics b{{20.0}, {3.0}, {40.0}, {30.0}, {false}};
    auto r = computeBudget(config, freqs, {a, b});
    REQUIRE(r.valid[0]);
    REQUIRE(r.gain_dB[0] == Approx(30.0));
    REQUIRE(r.nf_dB[0] == Approx(2.2645).margin(1e-3));
}
TEST_CASE("Budget cascade combines IIP3", "[budget]") {
    BudgetConfig config;
    config.stages = {BudgetStage{}, BudgetStage{}};
    std::vector<double> freqs{1e9};
    BudgetStageMetrics a{{10.0}, {2.0}, {30.0}, {20.0}, {false}};
    BudgetStageMetrics b{{20.0}, {3.0}, {40.0}, {30.0}, {false}};
    auto r = computeBudget(config, freqs, {a, b});
    REQUIRE(r.iip3_dBm[0] == Approx(9.586).margin(1e-3));
}
TEST_CASE("Budget cascade combines P1dB", "[budget]") {
    BudgetConfig config;
    config.stages = {BudgetStage{}, BudgetStage{}};
    std::vector<double> freqs{1e9};
    BudgetStageMetrics a{{10.0}, {2.0}, {30.0}, {20.0}, {false}};
    BudgetStageMetrics b{{20.0}, {3.0}, {40.0}, {30.0}, {false}};
    auto r = computeBudget(config, freqs, {a, b});
    REQUIRE(r.p1db_dBm[0] == Approx(29.586).margin(1e-3));
}
TEST_CASE("Budget output power adds total gain to input power", "[budget]") {
    BudgetConfig config;
    config.input_power_dBm = -30.0;
    config.stages = {BudgetStage{}, BudgetStage{}};
    BudgetStageMetrics a{{10.0}, {2.0}, {30.0}, {20.0}, {false}};
    BudgetStageMetrics b{{20.0}, {3.0}, {40.0}, {30.0}, {false}};
    auto r = computeBudget(config, {1e9}, {a, b});
    REQUIRE(r.output_power_dBm[0] == Approx(0.0));
}
TEST_CASE("Budget invalidates out-of-band and zero-gain points", "[budget]") {
    BudgetConfig config;
    config.stages = {BudgetStage{}, BudgetStage{}};
    std::vector<double> freqs{1e9, 2e9};
    BudgetStageMetrics a{{10.0, 10.0}, {2.0, 2.0}, {30.0, 30.0}, {20.0, 20.0}, {false, true}};
    BudgetStageMetrics b{{20.0, -std::numeric_limits<double>::infinity()},
                         {3.0, 3.0},
                         {40.0, 40.0},
                         {30.0, 30.0},
                         {false, false}};
    auto r = computeBudget(config, freqs, {a, b});
    REQUIRE(r.valid[0]);
    REQUIRE_FALSE(r.valid[1]);
    REQUIRE(std::isnan(r.gain_dB[1]));
    REQUIRE(std::isnan(r.nf_dB[1]));
}
TEST_CASE("Budget invalidates a non-finite or underflowing stage gain", "[budget]") {
    BudgetConfig config;
    config.stages = {BudgetStage{}, BudgetStage{}};
    std::vector<double> freqs{1e9, 2e9};

    // Point 1 has a non-finite gain with the stage in band, so the out-of-band flag from
    // the case above cannot be what invalidates it.
    BudgetStageMetrics a{{10.0, 10.0}, {2.0, 2.0}, {30.0, 30.0}, {20.0, 20.0}, {false, false}};
    BudgetStageMetrics b{{20.0, -std::numeric_limits<double>::infinity()},
                         {3.0, 3.0},
                         {40.0, 40.0},
                         {30.0, 30.0},
                         {false, false}};
    auto non_finite = computeBudget(config, freqs, {a, b});
    REQUIRE(non_finite.valid[0]);
    REQUIRE_FALSE(non_finite.valid[1]);
    REQUIRE(std::isnan(non_finite.gain_dB[1]));
    REQUIRE(std::isnan(non_finite.nf_dB[1]));
    REQUIRE(std::isnan(non_finite.output_power_dBm[1]));
    REQUIRE(std::isnan(non_finite.iip3_dBm[1]));
    REQUIRE(std::isnan(non_finite.p1db_dBm[1]));

    // A finite gain that underflows to a zero linear gain (~1e-400) must invalidate too.
    BudgetStageMetrics underflow{
        {10.0, -4000.0}, {2.0, 2.0}, {30.0, 30.0}, {20.0, 20.0}, {false, false}};
    BudgetStageMetrics after{{20.0, 20.0}, {3.0, 3.0}, {40.0, 40.0}, {30.0, 30.0}, {false, false}};
    auto zero_gain = computeBudget(config, freqs, {underflow, after});
    REQUIRE(zero_gain.valid[0]);
    REQUIRE_FALSE(zero_gain.valid[1]);
    REQUIRE(std::isnan(zero_gain.gain_dB[1]));
    REQUIRE(std::isnan(zero_gain.nf_dB[1]));
}
TEST_CASE("Budget passive-only IIP3 is not applicable", "[budget]") {
    BudgetConfig config;
    config.stages = {BudgetStage{}};
    BudgetStageMetrics passive{{-2.0},
                               {2.0},
                               {std::numeric_limits<double>::infinity()},
                               {std::numeric_limits<double>::infinity()},
                               {false}};
    auto r = computeBudget(config, {1e9}, {passive});
    REQUIRE(r.valid[0]);
    REQUIRE_FALSE(std::isfinite(r.iip3_dBm[0]));
}
TEST_CASE("Budget JSON round-trips stages and input power", "[budget]") {
    BudgetConfig config;
    config.input_power_dBm = -25.0;
    BudgetStage s;
    s.type = "amplifier";
    s.part_number = "AM1143";
    s.manufacturer = "Anatech";
    s.parameters = {{"gain_dB", 30.0}, {"nf_dB", 1.5}};
    s.data_files = {{"s_parameters", "AM1143.s2p"}};
    s.source_path = "/tmp/lib/am1143.json";
    config.stages.push_back(s);
    std::string diagnostic;
    auto restored = budgetFromJson(budgetToJson(config), diagnostic);
    REQUIRE(diagnostic.empty());
    REQUIRE(restored.input_power_dBm == Approx(-25.0));
    REQUIRE(restored.stages.size() == 1);
    REQUIRE(restored.stages[0].part_number == "AM1143");
    REQUIRE(restored.stages[0].parameters == s.parameters);
}
TEST_CASE("Budget JSON isolates and retains a malformed stage", "[budget]") {
    const nlohmann::json malformed = {{"type", 5}, {"part_number", "B"}, {"parameters", {}}};
    nlohmann::json j = {
        {"version", 1},
        {"input_power_dbm", -30.0},
        {"stages",
         nlohmann::json::array(
             {{{"type", "amplifier"}, {"part_number", "A"}, {"parameters", {}}}, malformed})}};
    std::string diagnostic;
    auto config = budgetFromJson(j, diagnostic);
    REQUIRE(config.stages.size() == 1);
    REQUIRE(config.stages[0].part_number == "A");
    REQUIRE(config.rejected_stages.size() == 1);
    REQUIRE(config.rejected_stages[0].raw == malformed);
    REQUIRE_FALSE(config.rejected_stages[0].reason.empty());
    REQUIRE(config.rejected_stages[0].position == 1);
    REQUIRE_FALSE(diagnostic.empty());

    // Saving after loading puts the rejected entry back unchanged, at its original position.
    const nlohmann::json saved = budgetToJson(config);
    REQUIRE(saved["stages"].size() == 2);
    REQUIRE(saved["stages"][0]["part_number"] == "A");
    REQUIRE(saved["stages"][1] == malformed);

    std::string second_diagnostic;
    const auto restored = budgetFromJson(saved, second_diagnostic);
    REQUIRE(restored.stages.size() == 1);
    REQUIRE(restored.rejected_stages.size() == 1);
    REQUIRE(restored.rejected_stages[0].raw == malformed);
}

TEST_CASE("Budget JSON rejects and retains an unsupported stage type", "[budget]") {
    const nlohmann::json unsupported = {{"type", "generator"}, {"part_number", "GEN"}};
    nlohmann::json j = {
        {"version", 1},
        {"stages",
         nlohmann::json::array({unsupported, {{"type", "amplifier"}, {"part_number", "A"}}})}};
    std::string diagnostic;
    auto config = budgetFromJson(j, diagnostic);
    REQUIRE(config.stages.size() == 1);
    REQUIRE(config.stages[0].part_number == "A");
    REQUIRE(config.rejected_stages.size() == 1);
    REQUIRE(config.rejected_stages[0].raw == unsupported);
    REQUIRE(diagnostic.find("generator") != std::string::npos);
}

TEST_CASE("Budget JSON preserves an unsupported version verbatim", "[budget]") {
    const nlohmann::json section = {
        {"version", 2},
        {"input_power_dbm", -3.0},
        {"future_field", {{"nested", true}}},
        {"stages", {{{"type", "future_part"}, {"part_number", "FUTURE"}}}}};
    std::string diagnostic;
    const auto config = budgetFromJson(section, diagnostic);
    REQUIRE(config.stages.empty());
    REQUIRE(config.rejected_stages.empty());
    REQUIRE(config.unsupported_section == section);
    REQUIRE_FALSE(config.unsupported_reason.empty());
    REQUIRE(diagnostic == config.unsupported_reason);
    // Nothing this build knows how to interpret is rewritten: the raw section round-trips.
    REQUIRE(budgetToJson(config) == section);
}

TEST_CASE("Budget JSON rejects a non-finite input power", "[budget]") {
    nlohmann::json j = {{"version", 1}};
    j["input_power_dbm"] = std::numeric_limits<double>::infinity();
    std::string diagnostic;
    const auto config = budgetFromJson(j, diagnostic);
    REQUIRE(config.input_power_dBm == Approx(-30.0));
    REQUIRE(diagnostic.find("finite") != std::string::npos);

    j["input_power_dbm"] = -std::numeric_limits<double>::infinity();
    diagnostic.clear();
    const auto negative = budgetFromJson(j, diagnostic);
    REQUIRE(negative.input_power_dBm == Approx(-30.0));
    REQUIRE(diagnostic.find("finite") != std::string::npos);
}

TEST_CASE("Budget JSON normalizes the ideal filter alias", "[budget]") {
    nlohmann::json j = {{"version", 1},
                        {"stages", {{{"type", "ideal_filter"}, {"part_number", "F"}}}}};
    std::string diagnostic;
    auto config = budgetFromJson(j, diagnostic);
    REQUIRE(diagnostic.empty());
    REQUIRE(config.stages.size() == 1);
    REQUIRE(config.stages[0].type == "filter");
}

TEST_CASE("Budget stage types are whitelisted and normalized", "[budget]") {
    REQUIRE(budgetSupportsStageType("amplifier"));
    REQUIRE(budgetSupportsStageType("ideal_filter"));
    REQUIRE_FALSE(budgetSupportsStageType("generator"));
    REQUIRE(budgetNormalizeStageType("ideal_filter") == "filter");
    REQUIRE(budgetNormalizeStageType("generator").empty());
}

TEST_CASE("Budget input power edits reject non-finite values", "[budget]") {
    BudgetConfig config;
    REQUIRE_FALSE(setBudgetInputPower(config, std::numeric_limits<double>::quiet_NaN()));
    REQUIRE(config.input_power_dBm == Approx(-30.0));
    REQUIRE_FALSE(setBudgetInputPower(config, std::numeric_limits<double>::infinity()));
    REQUIRE(config.input_power_dBm == Approx(-30.0));
    REQUIRE(setBudgetInputPower(config, -20.0));
    REQUIRE(config.input_power_dBm == Approx(-20.0));
    REQUIRE_FALSE(setBudgetInputPower(config, -20.0));
    REQUIRE(config.input_power_dBm == Approx(-20.0));
}
