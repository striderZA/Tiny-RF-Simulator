#include "common.h"
#include "receiver_requirements.h"
#include "receiver_requirements_widget.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using Catch::Approx;

namespace {
ReceiverRequirementsConfig allConfigured() {
    ReceiverRequirementsConfig c;
    c.band_start_Hz = 1.0e9;
    c.band_stop_Hz = 2.0e9;
    c.gain = ReceiverGainLimits{10.0, 20.0};
    c.nf_max_dB = 5.0;
    c.output_power = ReceiverOutputPowerLimits{-30.0, 0.0};
    c.iip3_min_dBm = 10.0;
    c.measurement_conditions.input_level_dBm = -30.0;
    c.measurement_conditions.iip3 = ReceiverIIP3TestSettings{1.0e6, -30.0, -20.0, 5.0};
    return c;
}

ReceiverRequirementsState configuredState() {
    ReceiverRequirementsState state;
    state.config = allConfigured();
    return state;
}

ReceiverRequirementsEvaluation
evaluate(const ReceiverRequirementsState &state, const std::vector<double> &gain,
         const std::vector<double> &nf, const std::vector<double> &output_power,
         const std::vector<double> &iip3, double sweep_start = 0.9e9, double sweep_stop = 2.1e9) {
    return evaluateReceiverRequirements(state, sweep_start, sweep_stop,
                                        {0.9e9, 1.0e9, 1.5e9, 2.0e9, 2.1e9}, gain, nf, output_power,
                                        iip3);
}

ReceiverRequirementsDraft gainDraft() {
    ReceiverRequirementsDraft d;
    d.band_start_Hz = "1e9";
    d.band_stop_Hz = "2e9";
    d.gain_enabled = true;
    d.gain_min_dB = "10";
    d.gain_max_dB = "20";
    return d;
}
} // namespace
TEST_CASE("ReceiverRequirements validates the input level range", "[receiver_requirements]") {
    auto c = allConfigured();
    c.measurement_conditions.input_level_dBm = MIN_POWER;
    CHECK_FALSE(validateReceiverRequirementsConfig(c).has_value());
    c.measurement_conditions.input_level_dBm = MAX_POWER;
    CHECK_FALSE(validateReceiverRequirementsConfig(c).has_value());
    for (double bad : {MIN_POWER - 1.0, MAX_POWER + 1.0, std::nan("")}) {
        c.measurement_conditions.input_level_dBm = bad;
        CHECK(validateReceiverRequirementsConfig(c) ==
              std::optional<std::string>("Input level must be between -174 and 10 dBm."));
    }
}

TEST_CASE("ReceiverRequirements draft requires an input level for output power",
          "[receiver_requirements]") {
    auto d = gainDraft();
    d.output_power_enabled = true;
    d.output_power_min_dBm = "-40";
    d.output_power_max_dBm = "0";
    std::string error;
    CHECK_FALSE(parseReceiverRequirementsDraft(d, error).has_value());
    CHECK(error == "Output power needs an input level (dBm).");
    d.input_level_dBm = "-30";
    const auto config = parseReceiverRequirementsDraft(d, error);
    REQUIRE(config.has_value());
    CHECK(config->measurement_conditions.input_level_dBm == -30.0);
}

TEST_CASE("ReceiverRequirements draft keeps an input level without output power",
          "[receiver_requirements]") {
    auto d = gainDraft();
    d.input_level_dBm = "-25";
    std::string error;
    CHECK(parseReceiverRequirementsDraft(d, error)->measurement_conditions.input_level_dBm ==
          -25.0);
    d.input_level_dBm = "";
    CHECK_FALSE(parseReceiverRequirementsDraft(d, error)->measurement_conditions.input_level_dBm);
}

TEST_CASE("ReceiverRequirements supports independent optional metric limits",
          "[receiver_requirements]") {
    ReceiverRequirementsConfig c;
    c.band_start_Hz = 1.0e9;
    c.band_stop_Hz = 2.0e9;
    CHECK_FALSE(validateReceiverRequirementsConfig(c).has_value());
    c.gain = ReceiverGainLimits{10.0, 20.0};
    CHECK_FALSE(validateReceiverRequirementsConfig(c).has_value());
    c.gain.reset();
    c.nf_max_dB = 5.0;
    CHECK_FALSE(validateReceiverRequirementsConfig(c).has_value());
    c.nf_max_dB.reset();
    c.output_power = ReceiverOutputPowerLimits{-30.0, 0.0};
    CHECK_FALSE(validateReceiverRequirementsConfig(c).has_value());
    c.output_power.reset();
    c.iip3_min_dBm = 10.0;
    c.measurement_conditions.iip3 = ReceiverIIP3TestSettings{1.0e6, -30.0, -20.0, 5.0};
    CHECK_FALSE(validateReceiverRequirementsConfig(c).has_value());
}

TEST_CASE("ReceiverRequirements evaluates each metric independently when others are disabled",
          "[receiver_requirements]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto checkOnly = [nan](auto disable) {
        auto state = configuredState();
        disable(*state.config);
        const auto result = evaluate(state, {0, 10, 15, 20, 0}, {0, 3, 4, 5, 0},
                                     {0, -30, -20, 0, 0}, {nan, 10, 15, 20, nan});
        CHECK(result.overall == ReceiverRequirementStatus::Pass);
        return result;
    };
    const auto gain = checkOnly([](auto &c) {
        c.nf_max_dB.reset();
        c.output_power.reset();
        c.iip3_min_dBm.reset();
    });
    CHECK(gain.gain.status == ReceiverRequirementStatus::Pass);
    CHECK(gain.noise_figure.status == ReceiverRequirementStatus::NotConfigured);

    const auto nf = checkOnly([](auto &c) {
        c.gain.reset();
        c.output_power.reset();
        c.iip3_min_dBm.reset();
    });
    CHECK(nf.noise_figure.status == ReceiverRequirementStatus::Pass);
    CHECK(nf.gain.status == ReceiverRequirementStatus::NotConfigured);

    const auto output = checkOnly([](auto &c) {
        c.gain.reset();
        c.nf_max_dB.reset();
        c.iip3_min_dBm.reset();
    });
    CHECK(output.output_power.status == ReceiverRequirementStatus::Pass);
    CHECK(output.gain.status == ReceiverRequirementStatus::NotConfigured);

    const auto iip3 = checkOnly([](auto &c) {
        c.gain.reset();
        c.nf_max_dB.reset();
        c.output_power.reset();
    });
    CHECK(iip3.iip3.status == ReceiverRequirementStatus::Pass);
    CHECK(iip3.gain.status == ReceiverRequirementStatus::NotConfigured);
}

TEST_CASE("ReceiverRequirements validates finite ordered metric bounds and IIP3 conditions",
          "[receiver_requirements]") {
    auto c = allConfigured();
    CHECK_FALSE(validateReceiverRequirementsConfig(c).has_value());
    c.output_power->maximum_dBm = std::numeric_limits<double>::infinity();
    CHECK(validateReceiverRequirementsConfig(c).has_value());
    c = allConfigured();
    c.output_power->minimum_dBm = 1.0;
    CHECK(validateReceiverRequirementsConfig(c).has_value());
    c = allConfigured();
    c.iip3_min_dBm = std::numeric_limits<double>::quiet_NaN();
    CHECK(validateReceiverRequirementsConfig(c).has_value());
    c = allConfigured();
    c.measurement_conditions.iip3->tone_spacing_Hz = 0.0;
    CHECK(validateReceiverRequirementsConfig(c).has_value());
    c = allConfigured();
    c.measurement_conditions.iip3->input_start_dBm = -10.0;
    CHECK(validateReceiverRequirementsConfig(c).has_value());
    c = allConfigured();
    c.measurement_conditions.iip3->input_step_dB = 6.0;
    CHECK(validateReceiverRequirementsConfig(c).has_value());
    c = allConfigured();
    c.measurement_conditions.input_level_dBm = std::numeric_limits<double>::quiet_NaN();
    CHECK(validateReceiverRequirementsConfig(c).has_value());
    c = allConfigured();
    c.band_start_Hz = std::numeric_limits<double>::quiet_NaN();
    CHECK(validateReceiverRequirementsConfig(c).has_value());
    c = allConfigured();
    c.band_start_Hz = 2.0e9;
    c.band_stop_Hz = 1.0e9;
    CHECK(validateReceiverRequirementsConfig(c).has_value());
    c = allConfigured();
    c.gain->minimum_dB = c.gain->maximum_dB + 1.0;
    CHECK(validateReceiverRequirementsConfig(c).has_value());
    c = allConfigured();
    c.gain->maximum_dB = std::numeric_limits<double>::infinity();
    CHECK(validateReceiverRequirementsConfig(c).has_value());
    c = allConfigured();
    c.nf_max_dB = std::numeric_limits<double>::quiet_NaN();
    CHECK(validateReceiverRequirementsConfig(c).has_value());
}

TEST_CASE(
    "ReceiverRequirements validates supplied measurement conditions when metrics are disabled",
    "[receiver_requirements]") {
    auto config = allConfigured();
    config.iip3_min_dBm.reset();
    config.measurement_conditions.iip3->tone_spacing_Hz = 0.0;
    CHECK(validateReceiverRequirementsConfig(config).has_value());

    config = allConfigured();
    config.output_power.reset();
    config.measurement_conditions.input_level_dBm = std::numeric_limits<double>::quiet_NaN();
    CHECK(validateReceiverRequirementsConfig(config).has_value());
}

TEST_CASE("ReceiverRequirements evaluates valid output samples with an input level",
          "[receiver_requirements]") {
    auto state = configuredState();
    state.config->measurement_conditions.input_level_dBm = -30.0;
    const auto result = evaluate(state, {0, 10, 15, 20, 0}, {0, 3, 4, 5, 0}, {0, -30, -20, 0, 0},
                                 {std::numeric_limits<double>::quiet_NaN(), 10, 15, 20,
                                  std::numeric_limits<double>::quiet_NaN()});
    CHECK(result.output_power.status == ReceiverRequirementStatus::Pass);
    CHECK(result.overall == ReceiverRequirementStatus::Pass);
}

TEST_CASE("ReceiverRequirements reports incomplete output data with an input level",
          "[receiver_requirements]") {
    auto state = configuredState();
    state.config->measurement_conditions.input_level_dBm = -30.0;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto result = evaluate(state, {0, 10, 15, 20, 0}, {0, 3, 4, 5, 0}, {0, nan, nan, nan, 0},
                                 {nan, 10, 15, 20, nan});
    CHECK(result.output_power.status == ReceiverRequirementStatus::Incomplete);
    CHECK(result.overall == ReceiverRequirementStatus::Incomplete);
}

TEST_CASE("ReceiverRequirements returns Not configured when every metric is disabled",
          "[receiver_requirements]") {
    ReceiverRequirementsState state;
    state.config = ReceiverRequirementsConfig{};
    state.config->band_start_Hz = 1.0e9;
    state.config->band_stop_Hz = 2.0e9;
    const auto result = evaluate(state, {}, {}, {}, {});
    CHECK(result.gain.status == ReceiverRequirementStatus::NotConfigured);
    CHECK(result.noise_figure.status == ReceiverRequirementStatus::NotConfigured);
    CHECK(result.output_power.status == ReceiverRequirementStatus::NotConfigured);
    CHECK(result.iip3.status == ReceiverRequirementStatus::NotConfigured);
    CHECK(result.overall == ReceiverRequirementStatus::NotConfigured);
}

TEST_CASE("ReceiverRequirements applies inclusive bounds and reports unit-neutral ranges",
          "[receiver_requirements]") {
    const auto result =
        evaluate(configuredState(), {0, 10, 15, 20, 0}, {0, 3, 4, 5, 0}, {0, -30, -20, 0, 0},
                 {std::numeric_limits<double>::quiet_NaN(), 10, 15, 20,
                  std::numeric_limits<double>::quiet_NaN()});
    CHECK(result.gain.status == ReceiverRequirementStatus::Pass);
    CHECK(result.gain.observed_min == Approx(10.0));
    CHECK(result.gain.observed_max == Approx(20.0));
    CHECK(result.noise_figure.status == ReceiverRequirementStatus::Pass);
    CHECK(result.noise_figure.observed_min == Approx(3.0));
    CHECK(result.noise_figure.observed_max == Approx(5.0));
    CHECK(result.output_power.status == ReceiverRequirementStatus::Pass);
    CHECK(result.output_power.observed_min == Approx(-30.0));
    CHECK(result.output_power.observed_max == Approx(0.0));
    CHECK(result.iip3.status == ReceiverRequirementStatus::Pass);
    CHECK(result.iip3.observed_min == Approx(10.0));
    CHECK(result.iip3.observed_max == Approx(20.0));
    CHECK(result.overall == ReceiverRequirementStatus::Pass);
}

TEST_CASE("ReceiverRequirements reports upper and lower limit violations over incomplete coverage",
          "[receiver_requirements]") {
    const auto state = configuredState();
    const std::vector<double> frequencies = {1.25e9, 1.75e9};
    const std::vector<double> good_nf = {3.0, 4.0};
    const std::vector<double> good_output = {-30.0, 0.0};
    const std::vector<double> good_iip3 = {15.0, 20.0};
    const auto evaluateSamples = [&](const std::vector<double> &gain, const std::vector<double> &nf,
                                     const std::vector<double> &output) {
        return evaluateReceiverRequirements(state, 1.0e9, 1.75e9, frequencies, gain, nf, output,
                                            good_iip3);
    };

    const auto gain_upper = evaluateSamples({15.0, 21.0}, good_nf, good_output);
    CHECK(gain_upper.gain.status == ReceiverRequirementStatus::Fail);
    CHECK(gain_upper.overall == ReceiverRequirementStatus::Fail);
    CHECK(gain_upper.noise_figure.status == ReceiverRequirementStatus::Incomplete);

    const auto nf_upper = evaluateSamples({15.0, 20.0}, {3.0, 6.0}, good_output);
    CHECK(nf_upper.noise_figure.status == ReceiverRequirementStatus::Fail);
    CHECK(nf_upper.overall == ReceiverRequirementStatus::Fail);
    CHECK(nf_upper.gain.status == ReceiverRequirementStatus::Incomplete);

    const auto output_lower = evaluateSamples({15.0, 20.0}, good_nf, {-31.0, -20.0});
    CHECK(output_lower.output_power.status == ReceiverRequirementStatus::Fail);
    CHECK(output_lower.overall == ReceiverRequirementStatus::Fail);
    CHECK(output_lower.gain.status == ReceiverRequirementStatus::Incomplete);

    const auto output_upper = evaluateSamples({15.0, 20.0}, good_nf, {-20.0, 1.0});
    CHECK(output_upper.output_power.status == ReceiverRequirementStatus::Fail);
    CHECK(output_upper.overall == ReceiverRequirementStatus::Fail);
    CHECK(output_upper.gain.status == ReceiverRequirementStatus::Incomplete);
}

TEST_CASE("assess compares infinite values and treats only NaN as missing",
          "[receiver_requirements]") {
    ReceiverRequirementsState state;
    ReceiverRequirementsConfig config;
    config.band_start_Hz = 1.0e9;
    config.band_stop_Hz = 2.0e9;
    config.gain = ReceiverGainLimits{10.0, 20.0};
    state.config = config;

    const std::vector<double> frequencies = {1.4e9, 1.6e9};
    const std::vector<double> good_nf = {3.0, 3.0};
    const std::vector<double> good_output = {-30.0, -30.0};
    const std::vector<double> good_iip3 = {15.0, 15.0};
    const double infinite = std::numeric_limits<double>::infinity();
    const double missing = std::numeric_limits<double>::quiet_NaN();

    const auto compared = evaluateReceiverRequirements(
        state, 1.0e9, 2.0e9, frequencies, {-infinite, 15.0}, good_nf, good_output, good_iip3);
    CHECK(compared.gain.status == ReceiverRequirementStatus::Fail);
    REQUIRE(compared.gain.observed_min.has_value());
    CHECK(*compared.gain.observed_min == -infinite);
    REQUIRE(compared.gain.observed_max.has_value());
    CHECK(*compared.gain.observed_max == 15.0);
    CHECK(compared.overall == ReceiverRequirementStatus::Fail);

    const auto incomplete = evaluateReceiverRequirements(
        state, 1.0e9, 2.0e9, frequencies, {missing, 15.0}, good_nf, good_output, good_iip3);
    CHECK(incomplete.gain.status == ReceiverRequirementStatus::Incomplete);
}

TEST_CASE("ReceiverRequirements rejected draft preserves state while valid Apply clears diagnostic",
          "[receiver_requirements]") {
    auto state = configuredState();
    state.invalid_reason = "retained diagnostic";
    std::string error;
    auto malformed = gainDraft();
    malformed.gain_max_dB = "nan";
    CHECK_FALSE(applyReceiverRequirementsDraft(state, malformed, error));
    REQUIRE(state.config.has_value());
    CHECK(state.config->band_start_Hz == 1.0e9);
    CHECK(state.config->band_stop_Hz == 2.0e9);
    REQUIRE(state.config->gain.has_value());
    CHECK(state.config->gain->minimum_dB == 10.0);
    CHECK(state.config->gain->maximum_dB == 20.0);
    CHECK(state.config->nf_max_dB == 5.0);
    REQUIRE(state.config->output_power.has_value());
    CHECK(state.config->output_power->minimum_dBm == -30.0);
    CHECK(state.config->output_power->maximum_dBm == 0.0);
    CHECK(state.config->measurement_conditions.input_level_dBm == -30.0);
    REQUIRE(state.config->measurement_conditions.iip3.has_value());
    CHECK(state.config->measurement_conditions.iip3->tone_spacing_Hz == 1.0e6);
    CHECK(state.config->measurement_conditions.iip3->input_start_dBm == -30.0);
    CHECK(state.config->measurement_conditions.iip3->input_stop_dBm == -20.0);
    CHECK(state.config->measurement_conditions.iip3->input_step_dB == 5.0);

    CHECK(state.config->iip3_min_dBm == 10.0);
    CHECK(state.invalid_reason == "retained diagnostic");

    REQUIRE(applyReceiverRequirementsDraft(state, gainDraft(), error));
    CHECK(state.invalid_reason.empty());
    REQUIRE(state.config.has_value());
    REQUIRE(state.config->gain.has_value());
    CHECK(state.config->gain->minimum_dB == 10.0);
    CHECK(state.config->gain->maximum_dB == 20.0);
}

TEST_CASE("ReceiverRequirements uses aligned IIP3 input levels and requires three points",
          "[receiver_requirements]") {
    auto c = allConfigured();
    c.measurement_conditions.iip3 = ReceiverIIP3TestSettings{2.0e6, -30.0, -19.0, 4.0};
    CHECK_FALSE(validateReceiverRequirementsConfig(c).has_value());
    c.measurement_conditions.iip3->input_stop_dBm = -23.0;
    CHECK(validateReceiverRequirementsConfig(c).has_value());
    c = allConfigured();
    c.measurement_conditions.iip3->input_step_dB = 10.0;
    CHECK(validateReceiverRequirementsConfig(c).has_value());
}

TEST_CASE("ReceiverRequirements bounds IIP3 input-level counting", "[receiver_requirements]") {
    CHECK(receiverIIP3LevelCount({1.0e6, -30.0, -20.0, 5.0}) == 3);
    CHECK(receiverIIP3LevelCount({1.0e6, -100.0, 0.0, 1.0}) == kMaxReceiverIIP3InputLevels);
    CHECK_FALSE(receiverIIP3LevelCount({1.0e6, -30.0, -21.0, 5.0}).has_value());
    CHECK_FALSE(receiverIIP3LevelCount({1.0e6, -30.0, 71.0, 1.0}).has_value());
    CHECK_FALSE(receiverIIP3LevelCount({1.0e6, -std::numeric_limits<double>::max(),
                                        std::numeric_limits<double>::max(), 1.0})
                    .has_value());
    CHECK_FALSE(receiverIIP3LevelCount({1.0e6, 1.0, 2.0, std::numeric_limits<double>::denorm_min()})
                    .has_value());
    CHECK_FALSE(receiverIIP3LevelCount({0.0, 0.0, std::numeric_limits<double>::max(),
                                        std::numeric_limits<double>::max()})
                    .has_value());
}

TEST_CASE("ReceiverRequirements applies common coverage, alignment, and precedence rules",
          "[receiver_requirements]") {
    const auto full_state = configuredState();
    auto partial = evaluate(full_state, {0, 10, 9, 20, 0}, {0, 3, 4, 5, 0}, {0, -30, -20, 0, 0},
                            {std::numeric_limits<double>::quiet_NaN(), 10, 15, 20,
                             std::numeric_limits<double>::quiet_NaN()},
                            1.1e9, 1.9e9);
    CHECK(partial.gain.status == ReceiverRequirementStatus::Fail);
    CHECK(partial.overall == ReceiverRequirementStatus::Fail);

    const auto prefix =
        evaluate(full_state, {0, 10, 15, 20, 0}, {0, 3, 4, 5, 0}, {0, -30, -20, 0, 0},
                 {std::numeric_limits<double>::quiet_NaN(), 9, 15}, 0.9e9, 2.1e9);
    CHECK(prefix.iip3.status == ReceiverRequirementStatus::Fail);

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto no_in_band = evaluateReceiverRequirements(full_state, 0.9e9, 2.1e9, {0.9e9, 2.1e9},
                                                         {0, 0}, {0, 0}, {0, 0}, {nan, nan});
    CHECK(no_in_band.gain.status == ReceiverRequirementStatus::Incomplete);
    CHECK(no_in_band.overall == ReceiverRequirementStatus::Incomplete);
}

TEST_CASE("ReceiverRequirements marks each mismatched metric vector incomplete",
          "[receiver_requirements]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto check = [&](int short_metric) {
        auto state = configuredState();
        const std::vector<double> gain = {0, 10, 15, 20, 0};
        const std::vector<double> nf = {0, 3, 4, 5, 0};
        const std::vector<double> output_power = {0, -30, -20, 0, 0};
        return evaluate(state, short_metric == 0 ? std::vector<double>{0, 10, 15, 20} : gain,
                        short_metric == 1 ? std::vector<double>{0, 3, 4, 5} : nf,
                        short_metric == 2 ? std::vector<double>{0, -30, -20, 0} : output_power,
                        short_metric == 3 ? std::vector<double>{nan, 10, 15, 20}
                                          : std::vector<double>{nan, 10, 15, 20, nan});
    };
    CHECK(check(0).gain.status == ReceiverRequirementStatus::Incomplete);
    CHECK(check(1).noise_figure.status == ReceiverRequirementStatus::Incomplete);
    CHECK(check(2).output_power.status == ReceiverRequirementStatus::Incomplete);
    CHECK(check(3).iip3.status == ReceiverRequirementStatus::Incomplete);
}

TEST_CASE("ReceiverRequirements marks invalid state and configuration invalid",
          "[receiver_requirements]") {
    auto state = configuredState();
    state.invalid_reason = "invalid";
    CHECK(evaluate(state, {}, {}, {}, {}).overall ==
          ReceiverRequirementStatus::InvalidConfiguration);
    state.invalid_reason.clear();
    state.config->band_stop_Hz = state.config->band_start_Hz;
    CHECK(evaluate(state, {}, {}, {}, {}).overall ==
          ReceiverRequirementStatus::InvalidConfiguration);
}

TEST_CASE("ReceiverRequirements preserves valid failures when other metric data is missing",
          "[receiver_requirements]") {
    const auto result = evaluate(configuredState(), {0, 9, 15, 20, 0},
                                 {0, 3, std::numeric_limits<double>::quiet_NaN(), 5, 0},
                                 {0, -30, -20, 0, 0}, {9, 15, 10});
    CHECK(result.gain.status == ReceiverRequirementStatus::Fail);
    CHECK(result.noise_figure.status == ReceiverRequirementStatus::Incomplete);
    CHECK(result.overall == ReceiverRequirementStatus::Fail);
}
TEST_CASE("ReceiverRequirements aligns IIP3 samples with the frequency sweep",
          "[receiver_requirements]") {
    const auto state = configuredState();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto failed_with_missing = evaluate(state, {0, 10, 15, 20, 0}, {0, 3, 4, 5, 0},
                                              {0, -30, -20, 0, 0}, {nan, 15, nan, 9, nan});
    CHECK(failed_with_missing.iip3.status == ReceiverRequirementStatus::Fail);
    CHECK(failed_with_missing.overall == ReceiverRequirementStatus::Fail);

    const auto complete = evaluate(state, {0, 10, 15, 20, 0}, {0, 3, 4, 5, 0}, {0, -30, -20, 0, 0},
                                   {nan, 10, 15, 20, nan});
    CHECK(complete.iip3.status == ReceiverRequirementStatus::Pass);

    const auto short_vector =
        evaluate(state, {0, 10, 15, 20, 0}, {0, 3, 4, 5, 0}, {0, -30, -20, 0, 0}, {nan, 10, 15});
    CHECK(short_vector.iip3.status == ReceiverRequirementStatus::Incomplete);
}

TEST_CASE("ReceiverRequirements requires full valid coverage for every enabled metric",
          "[receiver_requirements]") {
    const auto result = evaluate(configuredState(), {0, 15, 15, 20, 0}, {0, 3, 4, 5, 0},
                                 {0, -30, -20, 0, 0}, {10, 15}, 1.1e9, 1.9e9);
    CHECK(result.gain.status == ReceiverRequirementStatus::Incomplete);
    CHECK(result.iip3.status == ReceiverRequirementStatus::Incomplete);
    CHECK(result.overall == ReceiverRequirementStatus::Incomplete);
}

TEST_CASE(
    "ReceiverRequirements draft permits blank disabled limits and clears disabled configuration",
    "[receiver_requirements]") {
    auto d = gainDraft();
    std::string error;
    auto parsed = parseReceiverRequirementsDraft(d, error);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->gain.has_value());
    CHECK(parsed->gain->minimum_dB == Approx(10.0));
    CHECK(parsed->gain->maximum_dB == Approx(20.0));
    CHECK_FALSE(parsed->nf_max_dB.has_value());

    ReceiverRequirementsState state;
    REQUIRE(applyReceiverRequirementsDraft(state, d, error));
    d.gain_enabled = false;
    d.gain_min_dB.clear();
    d.gain_max_dB.clear();
    REQUIRE(applyReceiverRequirementsDraft(state, d, error));
    CHECK_FALSE(state.config.has_value());
}

TEST_CASE("ReceiverRequirements draft parses enabled output and IIP3 settings",
          "[receiver_requirements]") {
    auto d = gainDraft();
    d.output_power_enabled = true;
    d.output_power_min_dBm = "-30";
    d.output_power_max_dBm = "0";
    d.input_level_dBm = "-30";
    d.iip3_enabled = true;
    d.iip3_min_dBm = "10";
    d.iip3_tone_spacing_Hz = "1e6";
    d.iip3_input_start_dBm = "-30";
    d.iip3_input_stop_dBm = "-20";
    d.iip3_input_step_dB = "5";
    std::string error;
    const auto parsed = parseReceiverRequirementsDraft(d, error);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->output_power.has_value());
    CHECK(parsed->output_power->minimum_dBm == Approx(-30.0));
    CHECK(parsed->output_power->maximum_dBm == Approx(0.0));
    CHECK(parsed->measurement_conditions.input_level_dBm == Approx(-30.0));
    REQUIRE(parsed->measurement_conditions.iip3.has_value());
    CHECK(parsed->measurement_conditions.iip3->input_stop_dBm == Approx(-20.0));
}

TEST_CASE(
    "ReceiverRequirements draft rejects malformed enabled limits and allows no enabled metric",
    "[receiver_requirements]") {
    std::string error;
    auto d = gainDraft();
    d.gain_max_dB = "nan";
    CHECK_FALSE(parseReceiverRequirementsDraft(d, error).has_value());
    d.gain_enabled = false;
    d.gain_min_dB.clear();
    d.gain_max_dB.clear();
    ReceiverRequirementsState state;
    REQUIRE(applyReceiverRequirementsDraft(state, d, error));
    const auto result = evaluate(state, {}, {}, {}, {});
    CHECK(result.overall == ReceiverRequirementStatus::NotConfigured);
}

TEST_CASE("ReceiverRequirements maps outcomes to status tones", "[receiver_requirements]") {
    CHECK(receiverRequirementStatusTone(ReceiverRequirementStatus::Pass) ==
          ReceiverRequirementStatusTone::PassGreen);
    CHECK(receiverRequirementStatusTone(ReceiverRequirementStatus::Fail) ==
          ReceiverRequirementStatusTone::FailRed);
    CHECK(receiverRequirementStatusTone(ReceiverRequirementStatus::NotConfigured) ==
          ReceiverRequirementStatusTone::Neutral);
    CHECK(receiverRequirementStatusTone(ReceiverRequirementStatus::InvalidConfiguration) ==
          ReceiverRequirementStatusTone::Neutral);
    CHECK(receiverRequirementStatusTone(ReceiverRequirementStatus::Incomplete) ==
          ReceiverRequirementStatusTone::Neutral);
}

TEST_CASE("Receiver requirement modes add copies of the selected mode",
          "[receiver_requirements][modes]") {
    ReceiverRequirementsSet set;
    std::string error;
    REQUIRE(addReceiverRequirementsMode(set, "Band 1 High Gain", error));
    CHECK(set.modes.size() == 1);
    CHECK_FALSE(set.modes[0].config);
    set.modes[0].config = allConfigured();
    REQUIRE(addReceiverRequirementsMode(set, "  Band 1 Low Gain  ", error));
    CHECK(set.selected == 1);
    CHECK(set.modes[1].name == "Band 1 Low Gain");
    CHECK(set.modes[1].config == set.modes[0].config);
}

TEST_CASE("Receiver requirement mode names are checked", "[receiver_requirements][modes]") {
    ReceiverRequirementsSet set;
    std::string error;
    REQUIRE(addReceiverRequirementsMode(set, "Low Gain", error));
    const auto before = set;
    CHECK_FALSE(addReceiverRequirementsMode(set, " low gain ", error));
    CHECK(error == "A mode named 'low gain' already exists.");
    CHECK_FALSE(addReceiverRequirementsMode(set, "   ", error));
    CHECK(error == "Mode name must not be empty.");
    CHECK_FALSE(addReceiverRequirementsMode(set, std::string(65, 'x'), error));
    CHECK(error == "Mode name must be at most 64 characters.");
    CHECK(set == before);
    CHECK(renameReceiverRequirementsMode(set, 0, "LOW GAIN", error)); // own name, new case
    CHECK(set.modes[0].name == "LOW GAIN");
}

TEST_CASE("Receiver requirement mode count is capped", "[receiver_requirements][modes]") {
    ReceiverRequirementsSet set;
    std::string error;
    for (std::size_t i = 0; i < kMaxReceiverRequirementModes; ++i)
        REQUIRE(addReceiverRequirementsMode(set, "M" + std::to_string(i), error));
    CHECK_FALSE(addReceiverRequirementsMode(set, "extra", error));
    CHECK(error == "At most 64 modes are allowed.");
}

TEST_CASE("Removing a receiver requirement mode keeps the selection valid",
          "[receiver_requirements][modes]") {
    ReceiverRequirementsSet set;
    std::string error;
    for (auto name : {"A", "B", "C"})
        REQUIRE(addReceiverRequirementsMode(set, name, error));
    REQUIRE(set.selected == 2);
    REQUIRE(removeReceiverRequirementsMode(set, 2));
    CHECK(set.selected == 1);
    REQUIRE(removeReceiverRequirementsMode(set, 0));
    CHECK(set.selected == 0);
    CHECK(set.modes[0].name == "B");
    REQUIRE(removeReceiverRequirementsMode(set, 0));
    CHECK(set.modes.empty());
    CHECK(set.selected == 0);
    CHECK_FALSE(removeReceiverRequirementsMode(set, 0));
}

TEST_CASE("Applying a draft changes only the selected mode", "[receiver_requirements][modes]") {
    ReceiverRequirementsSet set;
    std::string error;
    CHECK_FALSE(applyReceiverRequirementsDraftToSelectedMode(set, gainDraft(), error));
    CHECK(error == "Add a mode before applying requirements.");

    REQUIRE(addReceiverRequirementsMode(set, "A", error));
    REQUIRE(addReceiverRequirementsMode(set, "B", error));
    REQUIRE(selectReceiverRequirementsMode(set, 0));

    REQUIRE(applyReceiverRequirementsDraftToSelectedMode(set, gainDraft(), error));
    REQUIRE(set.modes[0].config.has_value());
    CHECK(set.modes[0].config->band_start_Hz == 1.0e9);
    CHECK(set.modes[0].config->band_stop_Hz == 2.0e9);
    REQUIRE(set.modes[0].config->gain.has_value());
    CHECK(set.modes[0].config->gain->minimum_dB == 10.0);
    CHECK(set.modes[0].config->gain->maximum_dB == 20.0);
    CHECK_FALSE(set.modes[1].config.has_value());

    const auto before = set;
    auto malformed = gainDraft();
    malformed.band_start_Hz = "x";
    CHECK_FALSE(applyReceiverRequirementsDraftToSelectedMode(set, malformed, error));
    CHECK(error == "Requirement band endpoints must be finite numbers.");
    CHECK(set == before);

    ReceiverRequirementsDraft disabled;
    disabled.band_start_Hz = "1e9";
    disabled.band_stop_Hz = "2e9";
    REQUIRE(applyReceiverRequirementsDraftToSelectedMode(set, disabled, error));
    CHECK_FALSE(set.modes[0].config.has_value());
    CHECK_FALSE(set.modes[1].config.has_value());
}

TEST_CASE("A successful mode edit clears a retained invalid diagnostic",
          "[receiver_requirements][modes]") {
    ReceiverRequirementsSet set;
    set.invalid_reason = "bad file";
    std::string error;
    REQUIRE(addReceiverRequirementsMode(set, "A", error));
    CHECK(set.invalid_reason.empty());
}

TEST_CASE("Selected receiver requirements state follows the selection",
          "[receiver_requirements][modes]") {
    ReceiverRequirementsSet empty;
    auto state = selectedReceiverRequirementsState(empty);
    CHECK_FALSE(state.config.has_value());
    CHECK(state.invalid_reason.empty());

    ReceiverRequirementsSet invalid;
    invalid.invalid_reason = "bad file";
    state = selectedReceiverRequirementsState(invalid);
    CHECK_FALSE(state.config.has_value());
    CHECK(state.invalid_reason == "bad file");

    ReceiverRequirementsSet set;
    std::string error;
    REQUIRE(addReceiverRequirementsMode(set, "A", error));
    REQUIRE(addReceiverRequirementsMode(set, "B", error));
    set.modes[1].config = allConfigured();
    REQUIRE(selectReceiverRequirementsMode(set, 1));
    state = selectedReceiverRequirementsState(set);
    REQUIRE(state.config.has_value());
    CHECK(state.config == set.modes[1].config);
    CHECK(state.invalid_reason.empty());
}

TEST_CASE("Receiver requirements set validation reports the first problem",
          "[receiver_requirements][modes]") {
    ReceiverRequirementsSet duplicates;
    duplicates.modes.push_back({"X", std::nullopt});
    duplicates.modes.push_back({"x", std::nullopt});
    CHECK(validateReceiverRequirementsSet(duplicates) ==
          std::optional<std::string>("A mode named 'x' already exists."));

    ReceiverRequirementsSet padded;
    padded.modes.push_back({" A", std::nullopt});
    CHECK(validateReceiverRequirementsSet(padded) ==
          std::optional<std::string>("Mode name must not have leading or trailing spaces."));

    ReceiverRequirementsSet out_of_range;
    out_of_range.modes.push_back({"A", std::nullopt});
    out_of_range.selected = 1;
    CHECK(validateReceiverRequirementsSet(out_of_range) ==
          std::optional<std::string>("Selected mode is out of range."));

    ReceiverRequirementsSet bad_config;
    bad_config.modes.push_back({"A", allConfigured()});
    auto broken = allConfigured();
    broken.band_start_Hz = 2.0e9;
    broken.band_stop_Hz = 1.0e9;
    bad_config.modes.push_back({"B", broken});
    CHECK(validateReceiverRequirementsSet(bad_config) ==
          std::optional<std::string>("Mode 'B': Requirement band start must be below band stop."));

    ReceiverRequirementsSet valid;
    valid.modes.push_back({"A", allConfigured()});
    valid.modes.push_back({"B", std::nullopt});
    CHECK_FALSE(validateReceiverRequirementsSet(valid).has_value());
}

TEST_CASE("Receiver requirements JSON round-trips modes and selection",
          "[receiver_requirements][json]") {
    ReceiverRequirementsSet set;
    std::string error;
    REQUIRE(addReceiverRequirementsMode(set, "HG", error));
    set.modes[0].config = allConfigured();
    set.modes[0].config->measurement_conditions.input_level_dBm = -30.0;
    REQUIRE(addReceiverRequirementsMode(set, "LG", error));
    set.modes[1].config.reset();
    const auto encoded = receiverRequirementsToJson(set);
    CHECK(encoded.at("format") == 3);
    CHECK(encoded.at("selected_mode") == 1);
    CHECK(encoded.at("modes")[1].at("config").is_null());
    CHECK(encoded.at("modes")[0].at("config").at("measurement_conditions").at("input_level_dbm") ==
          -30.0);
    CHECK(receiverRequirementsFromJson(encoded) == set);

    ReceiverRequirementsSet empty;
    const auto encoded_empty = receiverRequirementsToJson(empty);
    CHECK((encoded_empty == nlohmann::json{{"format", 3},
                                           {"selected_mode", 0},
                                           {"next_mode_id", 1},
                                           {"modes", nlohmann::json::array()}}));
}

TEST_CASE("Receiver requirements JSON migrates a format 1 object to one Default mode",
          "[receiver_requirements][json]") {
    const nlohmann::json legacy = {
        {"band_start_hz", 1.0e9},
        {"band_stop_hz", 2.0e9},
        {"gain_min_db", 10.0},
        {"gain_max_db", 20.0},
        {"measurement_conditions", {{"output_reference_tone_frequency_hz", 1.0e9}}}};
    const auto set = receiverRequirementsFromJson(legacy);
    REQUIRE(set.invalid_reason.empty());
    REQUIRE(set.modes.size() == 1);
    CHECK(set.modes[0].name == "Default");
    CHECK(set.selected == 0);
    CHECK(set.modes[0].config->gain->maximum_dB == 20.0);
    CHECK_FALSE(set.modes[0].config->measurement_conditions.input_level_dBm);

    auto with_input_level = legacy;
    with_input_level["measurement_conditions"]["input_level_dbm"] = -30.0;
    const auto with_level = receiverRequirementsFromJson(with_input_level);
    REQUIRE(with_level.invalid_reason.empty());
    REQUIRE(with_level.modes.size() == 1);
    CHECK(with_level.modes[0].name == "Default");
    CHECK(with_level.modes[0].config->measurement_conditions.input_level_dBm == -30.0);
}

TEST_CASE("Receiver requirements JSON migrates a band-only legacy object",
          "[receiver_requirements][json]") {
    const nlohmann::json legacy = {{"band_start_hz", 1.0e9}, {"band_stop_hz", 2.0e9}};
    const auto set = receiverRequirementsFromJson(legacy);
    REQUIRE(set.invalid_reason.empty());
    REQUIRE(set.modes.size() == 1);
    CHECK(set.modes[0].name == "Default");
    CHECK(set.selected == 0);
    REQUIRE(set.modes[0].config.has_value());
    CHECK(set.modes[0].config->band_start_Hz == 1.0e9);
    CHECK(set.modes[0].config->band_stop_Hz == 2.0e9);
    CHECK_FALSE(set.modes[0].config->gain);
    CHECK_FALSE(set.modes[0].config->nf_max_dB);
    CHECK_FALSE(set.modes[0].config->output_power);
    CHECK_FALSE(set.modes[0].config->iip3_min_dBm);
    CHECK_FALSE(set.modes[0].config->measurement_conditions.input_level_dBm);
    CHECK_FALSE(set.modes[0].config->measurement_conditions.iip3);
}

TEST_CASE("Receiver requirements JSON keeps malformed data as invalid",
          "[receiver_requirements][json]") {
    const auto check_invalid = [](const ReceiverRequirementsSet &set, const std::string &reason) {
        CHECK(set.invalid_reason == reason);
        CHECK(set.modes.empty());
        CHECK(set.selected == 0);
    };

    check_invalid(receiverRequirementsFromJson(nlohmann::json{{"format", 4}}),
                  "Unsupported receiver requirements format 4.");

    check_invalid(receiverRequirementsFromJson(nlohmann::json{
                      {"format", 2},
                      {"selected_mode", 5},
                      {"modes", nlohmann::json::array({{{"name", "A"}, {"config", nullptr}}})}}),
                  "Selected mode is out of range.");

    check_invalid(receiverRequirementsFromJson(nlohmann::json{
                      {"format", 2},
                      {"modes", nlohmann::json::array({{{"name", "A"}, {"config", nullptr}},
                                                       {{"config", nullptr}}})}}),
                  "Mode 2: Mode name must be a string.");

    check_invalid(receiverRequirementsFromJson(nlohmann::json{
                      {"format", 2},
                      {"modes", nlohmann::json::array({{{"name", "A"}, {"config", nullptr}},
                                                       {{"name", "a"}, {"config", nullptr}}})}}),
                  "A mode named 'a' already exists.");

    auto bad_input_level = allConfigured();
    const auto encoded_bad_level = receiverRequirementsToJson(
        ReceiverRequirementsSet{{ReceiverRequirementsMode{"A", bad_input_level, 1}}, 0, {}, 2});
    auto malformed_level = encoded_bad_level;
    malformed_level["modes"][0]["config"]["measurement_conditions"]["input_level_dbm"] = "loud";
    check_invalid(receiverRequirementsFromJson(malformed_level),
                  "Mode 1: Input level must be a number.");

    ReceiverRequirementsSet marker;
    marker.invalid_reason = "x";
    const auto encoded_marker = receiverRequirementsToJson(marker);
    CHECK((encoded_marker == nlohmann::json{{"invalid_configuration", true}, {"diagnostic", "x"}}));
    check_invalid(receiverRequirementsFromJson(encoded_marker), "x");
}

TEST_CASE("ReceiverRequirements refuses '#' in mode names", "[receiver_requirements]") {
    ReceiverRequirementsSet set;
    set.modes.push_back({"HG##1", std::nullopt});
    const auto reason = validateReceiverRequirementsSet(set);
    REQUIRE(reason.has_value());
    CHECK(*reason == "Mode name must not contain '#'.");
    CHECK(receiverRequirementsModeNameError(ReceiverRequirementsSet{}, "Band #1", std::nullopt) ==
          "Mode name must not contain '#'.");
}

TEST_CASE("ReceiverRequirements ids are assigned once and never reused",
          "[receiver_requirements][ids]") {
    ReceiverRequirementsSet set;
    std::string error;
    REQUIRE(addReceiverRequirementsMode(set, "A", error));
    REQUIRE(addReceiverRequirementsMode(set, "B", error));
    CHECK(set.modes[0].id == 1);
    CHECK(set.modes[1].id == 2);
    CHECK(set.next_mode_id == 3);
    REQUIRE(removeReceiverRequirementsMode(set, 1));
    REQUIRE(addReceiverRequirementsMode(set, "B", error));
    CHECK(set.modes[1].id == 3);
    REQUIRE(renameReceiverRequirementsMode(set, 0, "A2", error));
    CHECK(set.modes[0].id == 1);
}

TEST_CASE("ReceiverRequirements legacy and format 2 objects migrate to ids",
          "[receiver_requirements][ids]") {
    const auto legacy = receiverRequirementsFromJson(
        nlohmann::json{{"band_start_hz", 1.0e9}, {"band_stop_hz", 2.0e9}});
    REQUIRE(legacy.invalid_reason.empty());
    REQUIRE(legacy.modes.size() == 1);
    CHECK(legacy.modes[0].name == "Default");
    CHECK(legacy.modes[0].id == 1);
    CHECK(legacy.next_mode_id == 2);

    const nlohmann::json format2 = {
        {"format", 2},
        {"selected_mode", 0},
        {"modes", {{{"name", "HG"}, {"config", nullptr}}, {{"name", "LG"}, {"config", nullptr}}}}};
    const auto set = receiverRequirementsFromJson(format2);
    REQUIRE(set.invalid_reason.empty());
    REQUIRE(set.modes.size() == 2);
    CHECK(set.modes[0].id == 1);
    CHECK(set.modes[1].id == 2);
    CHECK(set.next_mode_id == 3);
}

TEST_CASE("ReceiverRequirements format 3 round-trips ids", "[receiver_requirements][ids]") {
    ReceiverRequirementsSet set;
    set.modes = {{"A", std::nullopt, 4}, {"B", std::nullopt, 7}};
    set.next_mode_id = 9;
    const auto encoded = receiverRequirementsToJson(set);
    CHECK(encoded["format"] == 3);
    CHECK(encoded["next_mode_id"] == 9);
    CHECK(encoded["modes"][0]["id"] == 4);
    CHECK(receiverRequirementsFromJson(encoded) == set);
}

TEST_CASE("ReceiverRequirements format 3 refuses missing, repeated, or stale ids",
          "[receiver_requirements][ids]") {
    const nlohmann::json base_mode = {{"name", "A"}, {"config", nullptr}};
    const auto format3 = [&](nlohmann::json modes, nlohmann::json next) {
        return nlohmann::json{
            {"format", 3}, {"selected_mode", 0}, {"modes", modes}, {"next_mode_id", next}};
    };
    // Build the mode list as an explicit array: brace-initialising a json parameter with
    // {object} is compiler-dependent and can arrive as one object, which trips "modes must
    // be a list" instead of the per-mode error.
    CHECK(receiverRequirementsFromJson(format3(nlohmann::json::array({base_mode}), 2))
              .invalid_reason == "Mode 1: Mode id must be a positive integer.");
    CHECK(receiverRequirementsFromJson(
              format3(nlohmann::json::array({{{"id", 1}, {"name", "A"}, {"config", nullptr}}}),
                      nlohmann::json(nullptr)))
              .invalid_reason == "Next mode id must be a positive integer.");

    ReceiverRequirementsSet repeated;
    repeated.modes = {{"A", std::nullopt, 4}, {"B", std::nullopt, 4}};
    repeated.next_mode_id = 9;
    CHECK(validateReceiverRequirementsSet(repeated) ==
          std::optional<std::string>("Mode id 4 is used twice."));

    ReceiverRequirementsSet stale;
    stale.modes = {{"A", std::nullopt, 9}};
    stale.next_mode_id = 9;
    CHECK(validateReceiverRequirementsSet(stale) ==
          std::optional<std::string>("Mode id 9 is not below next_mode_id 9."));
}

TEST_CASE("Requirements edits keep ids by id in format 3 and by name in format 2",
          "[receiver_requirements][ids]") {
    ReceiverRequirementsSet current;
    current.modes = {{"A", std::nullopt, 1}, {"B", std::nullopt, 2}};
    current.next_mode_id = 3;
    std::string path;
    std::string error;

    const auto format3 = receiverRequirementsFromEdit(
        nlohmann::json{{"format", 3},
                       {"selected_mode", 0},
                       {"modes",
                        {{{"id", 2}, {"name", "Bee"}, {"config", nullptr}},
                         {{"name", "C"}, {"config", nullptr}}}}},
        current, path, error);
    REQUIRE(format3.has_value());
    CHECK(format3->modes[0].id == 2);
    CHECK(format3->modes[1].id == 3);
    CHECK(format3->next_mode_id == 4);

    const auto format2 =
        receiverRequirementsFromEdit(nlohmann::json{{"format", 2},
                                                    {"selected_mode", 0},
                                                    {"modes",
                                                     {{{"name", "a"}, {"config", nullptr}},
                                                      {{"name", "D"}, {"config", nullptr}}}}},
                                     current, path, error);
    REQUIRE(format2.has_value());
    CHECK(format2->modes[0].id == 1);
    CHECK(format2->modes[1].id == 3);
    CHECK(format2->next_mode_id == 4);

    const auto unknown = receiverRequirementsFromEdit(
        nlohmann::json{{"format", 3},
                       {"selected_mode", 0},
                       {"modes", {{{"id", 9}, {"name", "X"}, {"config", nullptr}}}}},
        current, path, error);
    CHECK_FALSE(unknown.has_value());
    CHECK(path == "/requirements/modes/0/id");
    CHECK(error == "no mode has id 9; omit id to add a mode");
}

TEST_CASE("out-of-band suppression points round-trip through JSON",
          "[receiver_requirements][json]") {
    ReceiverRequirementsSet set;
    ReceiverRequirementsConfig config = allConfigured();
    config.oob_suppression = {{ReceiverOOBSuppressionAnchor::BandStart, -5.0e6, 60.0},
                              {ReceiverOOBSuppressionAnchor::BandStop, 2.5e6, 40.0}};
    set.modes.push_back({"Wideband", config, 1});
    set.next_mode_id = 2;

    const auto encoded = receiverRequirementsToJson(set);
    const auto &points = encoded.at("modes").at(0).at("config").at("oob_suppression");
    REQUIRE(points.size() == 2);
    CHECK(points[0].at("anchor") == "band_start");
    CHECK(points[0].at("offset_hz") == -5.0e6);
    CHECK(points[0].at("min_db") == 60.0);
    CHECK(points[1].at("anchor") == "band_stop");
    CHECK(receiverRequirementsFromJson(encoded) == set);
}

TEST_CASE("out-of-band suppression JSON requires a valid point shape",
          "[receiver_requirements][json]") {
    const auto decode = [](nlohmann::json points) {
        return receiverRequirementsFromJson(nlohmann::json{
            {"format", 3},
            {"selected_mode", 0},
            {"next_mode_id", 2},
            {"modes", nlohmann::json::array({{{"id", 1},
                                              {"name", "A"},
                                              {"config",
                                               {{"band_start_hz", 1.0e9},
                                                {"band_stop_hz", 2.0e9},
                                                {"gain_min_db", 10.0},
                                                {"gain_max_db", 20.0},
                                                {"oob_suppression", std::move(points)}}}}})}});
    };

    CHECK(decode(nlohmann::json::array({{{"anchor", "band_start"}}})).invalid_reason ==
          "Mode 1: Out-of-band suppression point must carry anchor, offset_hz, and min_db.");
    CHECK(decode(nlohmann::json::array(
                     {nlohmann::json{{"anchor", "middle"}, {"offset_hz", 1.0}, {"min_db", 60.0}}}))
              .invalid_reason ==
          "Mode 1: Out-of-band suppression anchor must be 'band_start' or 'band_stop'.");
    CHECK(decode(nlohmann::json::array({nlohmann::json{
                     {"anchor", "band_start"}, {"offset_hz", "near"}, {"min_db", 60.0}}}))
              .invalid_reason == "Mode 1: Out-of-band offset must be a number.");
    CHECK(decode(nlohmann::json::array({nlohmann::json{
                     {"anchor", "band_start"}, {"offset_hz", -5.0e6}, {"min_db", false}}}))
              .invalid_reason == "Mode 1: Out-of-band suppression minimum must be a number.");
}

TEST_CASE("out-of-band suppression requires a point outside the band and a gain limit",
          "[receiver_requirements]") {
    const auto reason = [](const ReceiverOOBSuppressionPoint &point) {
        auto config = allConfigured();
        config.oob_suppression = {point};
        return validateReceiverRequirementsConfig(config);
    };

    CHECK(reason({ReceiverOOBSuppressionAnchor::BandStart, 0.0, 60.0}) ==
          std::optional<std::string>(
              "Out-of-band suppression frequency must lie outside the requirement band."));
    CHECK(reason({ReceiverOOBSuppressionAnchor::BandStart, 10.0e6, 60.0}) ==
          std::optional<std::string>(
              "Out-of-band suppression frequency must lie outside the requirement band."));
    CHECK(reason({ReceiverOOBSuppressionAnchor::BandStop, -10.0e6, 60.0}) ==
          std::optional<std::string>(
              "Out-of-band suppression frequency must lie outside the requirement band."));
    CHECK(reason({ReceiverOOBSuppressionAnchor::BandStart, -2.0e9, 60.0}) ==
          std::optional<std::string>(
              "Out-of-band suppression frequency must be finite and positive."));
    CHECK(reason({ReceiverOOBSuppressionAnchor::BandStart, std::numeric_limits<double>::quiet_NaN(),
                  60.0}) == std::optional<std::string>("Out-of-band offset must be finite."));
    CHECK(reason({ReceiverOOBSuppressionAnchor::BandStart, -5.0e6, -1.0}) ==
          std::optional<std::string>("Out-of-band suppression must be finite and non-negative."));
    CHECK(reason({ReceiverOOBSuppressionAnchor::BandStart, -5.0e6,
                  std::numeric_limits<double>::quiet_NaN()}) ==
          std::optional<std::string>("Out-of-band suppression must be finite and non-negative."));

    auto without_gain = allConfigured();
    without_gain.gain.reset();
    without_gain.oob_suppression = {{ReceiverOOBSuppressionAnchor::BandStart, -5.0e6, 60.0}};
    CHECK(validateReceiverRequirementsConfig(without_gain) ==
          std::optional<std::string>("Out-of-band suppression requires gain limits."));

    auto too_many = allConfigured();
    too_many.oob_suppression.assign(kMaxReceiverOOBSuppressionPoints + 1,
                                    {ReceiverOOBSuppressionAnchor::BandStart, -5.0e6, 60.0});
    CHECK(validateReceiverRequirementsConfig(too_many) ==
          std::optional<std::string>("At most 16 out-of-band suppression points are allowed."));

    auto valid = allConfigured();
    valid.oob_suppression = {{ReceiverOOBSuppressionAnchor::BandStart, -5.0e6, 60.0}};
    CHECK_FALSE(validateReceiverRequirementsConfig(valid).has_value());
}

TEST_CASE("out-of-band suppression compares against the in-band gain minimum",
          "[receiver_requirements]") {
    ReceiverRequirementsState state;
    ReceiverRequirementsConfig config;
    config.band_start_Hz = 1.0e9;
    config.band_stop_Hz = 2.0e9;
    config.gain = ReceiverGainLimits{10.0, 20.0};
    config.oob_suppression = {{ReceiverOOBSuppressionAnchor::BandStart, -5.0e6, 120.0}};
    state.config = config;

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const std::vector<double> frequencies = {0.995e9, 1.0e9, 2.0e9};
    const auto evaluate = [&](double oob_gain) {
        return evaluateReceiverRequirements(state, 0.995e9, 2.0e9, frequencies,
                                            {oob_gain, 15.0, 15.0}, {}, {}, {}, true);
    };

    // Threshold is gain_min - min_db = 10 - 120 = -110 dB. The gain maximum is irrelevant.
    const auto at_limit = evaluate(-110.0);
    REQUIRE(at_limit.oob_points.size() == 1);
    CHECK(at_limit.oob_points[0].status == ReceiverRequirementStatus::Pass);
    CHECK(at_limit.oob_points[0].frequency_Hz == 0.995e9);
    CHECK(at_limit.oob_points[0].observed_gain_dB == Approx(-110.0));
    CHECK(at_limit.oob_points[0].suppression_dB == Approx(120.0));
    CHECK(at_limit.oob_suppression.status == ReceiverRequirementStatus::Pass);
    CHECK(at_limit.oob_suppression.observed_min == Approx(120.0));
    CHECK(at_limit.overall == ReceiverRequirementStatus::Pass);

    const auto too_little = evaluate(-109.999);
    CHECK(too_little.oob_points[0].status == ReceiverRequirementStatus::Fail);
    CHECK(too_little.overall == ReceiverRequirementStatus::Fail);

    // A value below the budget's -100 dB readout floor still fails on its true value.
    const auto below_floor = evaluate(-105.0);
    CHECK(below_floor.oob_points[0].status == ReceiverRequirementStatus::Fail);
    CHECK(below_floor.oob_points[0].observed_gain_dB == Approx(-105.0));
    CHECK(below_floor.overall == ReceiverRequirementStatus::Fail);

    const auto deeper = evaluate(-115.0);
    CHECK(deeper.oob_points[0].status == ReceiverRequirementStatus::Pass);
    CHECK(deeper.oob_points[0].suppression_dB == Approx(125.0));

    const auto missing = evaluate(nan);
    CHECK(missing.oob_points[0].status == ReceiverRequirementStatus::Incomplete);
    CHECK_FALSE(missing.oob_points[0].observed_gain_dB.has_value());
    CHECK(missing.oob_points[0].suppression_dB.has_value() == false);
    CHECK(missing.overall == ReceiverRequirementStatus::Incomplete);

    const auto infinite = evaluate(-inf);
    CHECK(infinite.oob_points[0].status == ReceiverRequirementStatus::Pass);
    CHECK(infinite.oob_points[0].suppression_dB == inf);
    CHECK(infinite.overall == ReceiverRequirementStatus::Pass);

    // The live measurement path calls the evaluator without OOB and stays in-band-only.
    const auto ignored = evaluateReceiverRequirements(state, 0.995e9, 2.0e9, frequencies,
                                                      {-105.0, 15.0, 15.0}, {}, {}, {});
    CHECK(ignored.oob_points.empty());
    CHECK(ignored.oob_suppression.status == ReceiverRequirementStatus::NotConfigured);
    CHECK(ignored.overall == ReceiverRequirementStatus::Pass);
}

TEST_CASE("out-of-band suppression reads its own sample near a band edge",
          "[receiver_requirements]") {
    ReceiverRequirementsState state;
    ReceiverRequirementsConfig config;
    config.band_start_Hz = 1.0e9;
    config.band_stop_Hz = 2.0e9;
    config.gain = ReceiverGainLimits{10.0, 20.0};
    config.oob_suppression = {{ReceiverOOBSuppressionAnchor::BandStop, 0.5, 110.0}};
    state.config = config;

    // The in-band band-stop sample sorts first and lies inside the 1 Hz match tolerance; the
    // point's own sample must win, so the verdict reads -105 dB instead of the edge's +15 dB.
    const std::vector<double> frequencies = {2.0e9, 2.0e9 + 0.5};
    const auto result = evaluateReceiverRequirements(state, 1.0e9, 2.0e9 + 0.5, frequencies,
                                                     {15.0, -105.0}, {}, {}, {}, true);
    REQUIRE(result.oob_points.size() == 1);
    CHECK(result.oob_points[0].frequency_Hz == 2.0e9 + 0.5);
    CHECK(result.oob_points[0].observed_gain_dB == Approx(-105.0));
    CHECK(result.oob_points[0].status == ReceiverRequirementStatus::Pass);
    CHECK(result.oob_suppression.status == ReceiverRequirementStatus::Pass);
}

TEST_CASE("out-of-band suppression draft parses rows and refuses malformed numbers",
          "[receiver_requirements]") {
    auto draft = gainDraft();
    draft.oob_suppression = {{0, "-5000000", "60"}, {1, "2500000", "40"}};
    std::string error;
    const auto config = parseReceiverRequirementsDraft(draft, error);
    REQUIRE(config.has_value());
    REQUIRE(config->oob_suppression.size() == 2);
    CHECK(config->oob_suppression[0].anchor == ReceiverOOBSuppressionAnchor::BandStart);
    CHECK(config->oob_suppression[0].offset_Hz == -5.0e6);
    CHECK(config->oob_suppression[0].min_dB == 60.0);
    CHECK(config->oob_suppression[1].anchor == ReceiverOOBSuppressionAnchor::BandStop);
    CHECK(config->oob_suppression[1].offset_Hz == 2.5e6);

    draft.oob_suppression[0].offset_Hz = "x";
    CHECK_FALSE(parseReceiverRequirementsDraft(draft, error).has_value());
    CHECK(error == "Out-of-band offset must be a finite number.");

    draft.oob_suppression[0].offset_Hz = "-5000000";
    draft.oob_suppression[0].min_dB = "x";
    CHECK_FALSE(parseReceiverRequirementsDraft(draft, error).has_value());
    CHECK(error == "Out-of-band suppression must be a finite number.");

    draft.oob_suppression[0].min_dB = "60";
    ReceiverRequirementsState state;
    state.config = allConfigured();
    REQUIRE(applyReceiverRequirementsDraft(state, draft, error));
    REQUIRE(state.config.has_value());
    CHECK(state.config->oob_suppression == config->oob_suppression);

    // A config without any limit, including OOB, clears the mode.
    ReceiverRequirementsDraft empty;
    empty.band_start_Hz = "1e9";
    empty.band_stop_Hz = "2e9";
    REQUIRE(applyReceiverRequirementsDraft(state, empty, error));
    CHECK_FALSE(state.config.has_value());
}

TEST_CASE("ReceiverRequirements judges an SNR minimum over the band, worst case",
          "[receiver_requirements]") {
    ReceiverRequirementsConfig config;
    config.band_start_Hz = 1.0e9;
    config.band_stop_Hz = 2.0e9;
    config.snr = ReceiverSNRLimit{10.0, 1.0e6};
    ReceiverRequirementsState state;
    state.config = config;
    const std::vector<double> frequencies{0.9e9, 1.0e9, 1.5e9, 2.0e9, 2.1e9};
    const std::vector<double> unused(frequencies.size(), std::numeric_limits<double>::quiet_NaN());
    // The 0.9 GHz and 2.1 GHz points lie outside the band and do not count toward the verdict.
    const auto snrAt = [&](const std::vector<double> &snr_dB) {
        return evaluateReceiverRequirements(state, 0.9e9, 2.1e9, frequencies, unused, unused,
                                            unused, unused, false, snr_dB);
    };

    const auto pass = snrAt({50.0, 12.0, 11.0, 30.0, 40.0});
    CHECK(pass.snr.status == ReceiverRequirementStatus::Pass);
    REQUIRE(pass.snr.observed_min.has_value());
    CHECK(*pass.snr.observed_min == Approx(11.0));
    CHECK(*pass.snr.observed_max == Approx(30.0));
    CHECK(pass.overall == ReceiverRequirementStatus::Pass);

    const auto fail = snrAt({50.0, 12.0, 9.0, 30.0, 40.0});
    CHECK(fail.snr.status == ReceiverRequirementStatus::Fail);
    CHECK(fail.overall == ReceiverRequirementStatus::Fail);

    // A measured view without an SNR series cannot judge the limit.
    const auto missing = snrAt({});
    CHECK(missing.snr.status == ReceiverRequirementStatus::Incomplete);
    CHECK(missing.overall == ReceiverRequirementStatus::Incomplete);
}

TEST_CASE("ReceiverRequirements fails the mode when only the SNR limit fails",
          "[receiver_requirements]") {
    ReceiverRequirementsConfig config;
    config.band_start_Hz = 1.0e9;
    config.band_stop_Hz = 2.0e9;
    config.gain = ReceiverGainLimits{10.0, 20.0};
    config.snr = ReceiverSNRLimit{10.0, 1.0e6};
    ReceiverRequirementsState state;
    state.config = config;
    const std::vector<double> frequencies{1.0e9, 1.5e9, 2.0e9};
    const std::vector<double> gain(frequencies.size(), 15.0);
    const std::vector<double> unused(frequencies.size(), std::numeric_limits<double>::quiet_NaN());
    const auto result = evaluateReceiverRequirements(state, 1.0e9, 2.0e9, frequencies, gain, unused,
                                                     unused, unused, false, {5.0, 5.0, 5.0});
    CHECK(result.gain.status == ReceiverRequirementStatus::Pass);
    CHECK(result.snr.status == ReceiverRequirementStatus::Fail);
    CHECK(result.overall == ReceiverRequirementStatus::Fail);
}

TEST_CASE("ReceiverRequirements validates the SNR limit", "[receiver_requirements]") {
    auto c = allConfigured();
    c.snr = ReceiverSNRLimit{10.0, 1.0e6};
    CHECK_FALSE(validateReceiverRequirementsConfig(c).has_value());
    c.snr = ReceiverSNRLimit{std::numeric_limits<double>::quiet_NaN(), 1.0e6};
    CHECK(validateReceiverRequirementsConfig(c) ==
          std::optional<std::string>("SNR minimum must be finite."));
    for (double bad : {0.0, -1.0e6, std::numeric_limits<double>::quiet_NaN()}) {
        c.snr = ReceiverSNRLimit{10.0, bad};
        CHECK(validateReceiverRequirementsConfig(c) ==
              std::optional<std::string>("SNR bandwidth must be finite and positive."));
    }
}

TEST_CASE("ReceiverRequirements stores the SNR limit as an snr object", "[receiver_requirements]") {
    ReceiverRequirementsSet set;
    ReceiverRequirementsMode mode;
    mode.name = "Band 1";
    mode.id = 1;
    mode.config = allConfigured();
    mode.config->snr = ReceiverSNRLimit{10.0, 1.0e6};
    set.modes.push_back(mode);
    set.next_mode_id = 2;

    nlohmann::json json = receiverRequirementsToJson(set);
    CHECK(json["modes"][0]["config"]["snr"] ==
          nlohmann::json({{"min_db", 10.0}, {"bandwidth_hz", 1.0e6}}));
    const ReceiverRequirementsSet loaded = receiverRequirementsFromJson(json);
    REQUIRE(loaded.invalid_reason.empty());
    REQUIRE(loaded.modes.size() == 1);
    REQUIRE(loaded.modes[0].config.has_value());
    CHECK(loaded.modes[0].config->snr == mode.config->snr);

    json["modes"][0]["config"]["snr"]["min_db"] = "10";
    CHECK_FALSE(receiverRequirementsFromJson(json).invalid_reason.empty());
    json["modes"][0]["config"]["snr"] = {{"min_db", 10.0}};
    CHECK_FALSE(receiverRequirementsFromJson(json).invalid_reason.empty());
}

TEST_CASE("ReceiverRequirements drafts an SNR limit that needs an input level",
          "[receiver_requirements]") {
    ReceiverRequirementsDraft draft;
    draft.band_start_Hz = "1e9";
    draft.band_stop_Hz = "2e9";
    draft.snr_enabled = true;
    draft.snr_min_dB = "10";
    draft.snr_bandwidth_Hz = "1e6";
    std::string error;
    CHECK_FALSE(parseReceiverRequirementsDraft(draft, error).has_value());
    CHECK(error == "SNR needs an input level (dBm).");

    draft.input_level_dBm = "-30";
    const auto config = parseReceiverRequirementsDraft(draft, error);
    REQUIRE(config.has_value());
    REQUIRE(config->snr.has_value());
    CHECK(config->snr->minimum_dB == 10.0);
    CHECK(config->snr->bandwidth_Hz == 1.0e6);

    draft.snr_bandwidth_Hz = "0";
    CHECK_FALSE(parseReceiverRequirementsDraft(draft, error).has_value());
    CHECK(error == "SNR bandwidth must be finite and positive.");
}

TEST_CASE("an SNR-only draft keeps the mode configured", "[receiver_requirements]") {
    ReceiverRequirementsDraft draft;
    draft.band_start_Hz = "1e9";
    draft.band_stop_Hz = "2e9";
    draft.snr_enabled = true;
    draft.snr_min_dB = "10";
    draft.snr_bandwidth_Hz = "1e6";
    draft.input_level_dBm = "-30";
    ReceiverRequirementsState state;
    std::string error;
    REQUIRE(applyReceiverRequirementsDraft(state, draft, error));
    REQUIRE(state.config.has_value());
    REQUIRE(state.config->snr.has_value());
}
