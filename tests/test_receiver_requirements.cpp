#include "receiver_requirements.h"
#include "receiver_requirements_widget.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
    c.measurement_conditions.output_reference_tone_frequency_Hz = 1.5e9;
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
    c.measurement_conditions.output_reference_tone_frequency_Hz =
        std::numeric_limits<double>::quiet_NaN();
    CHECK(validateReceiverRequirementsConfig(c).has_value());
}

TEST_CASE("ReceiverRequirements treats output samples as authoritative without a tone selector",
          "[receiver_requirements]") {
    auto state = configuredState();
    state.config->measurement_conditions.output_reference_tone_frequency_Hz.reset();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto result = evaluate(state, {0, 10, 15, 20, 0}, {0, 3, 4, 5, 0}, {0, nan, nan, nan, 0},
                                 {nan, 10, 15, 20, nan});
    CHECK(result.output_power.status == ReceiverRequirementStatus::Incomplete);
    CHECK(result.overall == ReceiverRequirementStatus::Incomplete);
}

TEST_CASE("ReceiverRequirements does not gate output samples by selector band location",
          "[receiver_requirements]") {
    auto state = configuredState();
    state.config->measurement_conditions.output_reference_tone_frequency_Hz = 2.5e9;
    const auto result = evaluate(state, {0, 10, 15, 20, 0}, {0, 3, 4, 5, 0}, {0, -30, -20, 0, 0},
                                 {std::numeric_limits<double>::quiet_NaN(), 10, 15, 20,
                                  std::numeric_limits<double>::quiet_NaN()});
    CHECK(result.output_power.status == ReceiverRequirementStatus::Pass);
    CHECK(result.overall == ReceiverRequirementStatus::Pass);
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
    d.output_reference_tone_frequency_Hz = 1.5e9;
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
