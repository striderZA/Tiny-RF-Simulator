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
TEST_CASE("Budget JSON isolates a malformed stage", "[budget]") {
    nlohmann::json j = {{"version", 1},
                        {"input_power_dbm", -30.0},
                        {"stages",
                         {{{"type", "amplifier"}, {"part_number", "A"}, {"parameters", {}}},
                          {{"type", 5}, {"part_number", "B"}, {"parameters", {}}}}}};
    std::string diagnostic;
    auto config = budgetFromJson(j, diagnostic);
    REQUIRE(config.stages.size() == 1);
    REQUIRE(config.stages[0].part_number == "A");
    REQUIRE_FALSE(diagnostic.empty());
}
