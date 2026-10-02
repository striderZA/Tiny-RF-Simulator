#include "receiver_requirements.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>
#include <string>
#include <vector>

using Catch::Approx;

namespace {
ReceiverRequirementsState configuredState() {
    ReceiverRequirementsState state;
    state.config = ReceiverRequirementsConfig{1e9, 2e9, 10.0, 20.0, 5.0};
    return state;
}

ReceiverRequirementsEvaluation evaluate(const ReceiverRequirementsState &state,
                                        double sweep_start_Hz, double sweep_stop_Hz,
                                        const std::vector<double> &frequencies_Hz,
                                        const std::vector<double> &gain_dB,
                                        const std::vector<double> &noise_figure_dB) {
    return evaluateReceiverRequirements(state, sweep_start_Hz, sweep_stop_Hz, frequencies_Hz,
                                        gain_dB, noise_figure_dB);
}

std::vector<double> frequencies() { return {0.9e9, 1.0e9, 1.5e9, 2.0e9, 2.1e9}; }
} // namespace

TEST_CASE("ReceiverRequirements distinguishes absent and invalid configuration",
          "[receiver_requirements]") {
    const std::vector<double> f = frequencies();
    const std::vector<double> gain(f.size(), 15.0);
    const std::vector<double> nf(f.size(), 3.0);

    const ReceiverRequirementsState absent;
    const auto not_configured = evaluate(absent, 0.9e9, 2.1e9, f, gain, nf);
    CHECK(not_configured.gain.status == ReceiverRequirementStatus::NotConfigured);
    CHECK(not_configured.noise_figure.status == ReceiverRequirementStatus::NotConfigured);
    CHECK(not_configured.overall == ReceiverRequirementStatus::NotConfigured);

    ReceiverRequirementsState invalid;
    invalid.invalid_reason = "malformed persisted limits";
    const auto invalid_result = evaluate(invalid, 0.9e9, 2.1e9, f, gain, nf);
    CHECK(invalid_result.gain.status == ReceiverRequirementStatus::InvalidConfiguration);
    CHECK(invalid_result.noise_figure.status == ReceiverRequirementStatus::InvalidConfiguration);
    CHECK(invalid_result.overall == ReceiverRequirementStatus::InvalidConfiguration);
}

TEST_CASE("ReceiverRequirements rejects non-finite and reversed requirement limits",
          "[receiver_requirements]") {
    ReceiverRequirementsConfig config{1e9, 2e9, 10.0, 20.0, 5.0};
    CHECK_FALSE(validateReceiverRequirementsConfig(config).has_value());

    config.band_start_Hz = std::numeric_limits<double>::quiet_NaN();
    CHECK(validateReceiverRequirementsConfig(config).has_value());

    config = {2e9, 1e9, 10.0, 20.0, 5.0};
    CHECK(validateReceiverRequirementsConfig(config).has_value());

    config = {1e9, 2e9, 21.0, 20.0, 5.0};
    CHECK(validateReceiverRequirementsConfig(config).has_value());
}

TEST_CASE("ReceiverRequirements treats gain and NF boundaries as inclusive",
          "[receiver_requirements]") {
    const auto state = configuredState();
    const std::vector<double> f = frequencies();
    const std::vector<double> gain = {std::numeric_limits<double>::quiet_NaN(), 10.0, 15.0, 20.0,
                                      std::numeric_limits<double>::quiet_NaN()};
    const std::vector<double> nf = {std::numeric_limits<double>::quiet_NaN(), 5.0, 3.0, 0.0,
                                    std::numeric_limits<double>::quiet_NaN()};

    const auto result = evaluate(state, 0.9e9, 2.1e9, f, gain, nf);
    REQUIRE(result.gain.status == ReceiverRequirementStatus::Pass);
    REQUIRE(result.noise_figure.status == ReceiverRequirementStatus::Pass);
    CHECK(result.overall == ReceiverRequirementStatus::Pass);
    REQUIRE(result.gain.observed_min_dB.has_value());
    REQUIRE(result.gain.observed_max_dB.has_value());
    CHECK(*result.gain.observed_min_dB == Approx(10.0));
    CHECK(*result.gain.observed_max_dB == Approx(20.0));
    REQUIRE(result.noise_figure.observed_max_dB.has_value());
    CHECK(*result.noise_figure.observed_max_dB == Approx(5.0));
}

TEST_CASE("ReceiverRequirements evaluates worst-case in-band samples", "[receiver_requirements]") {
    const auto state = configuredState();
    const std::vector<double> f = frequencies();
    std::vector<double> gain = {0.0, 10.0, 15.0, 20.0, 0.0};
    const std::vector<double> nf = {0.0, 2.0, 3.0, 4.0, 0.0};

    gain[2] = 20.01;
    const auto result = evaluate(state, 0.9e9, 2.1e9, f, gain, nf);
    CHECK(result.gain.status == ReceiverRequirementStatus::Fail);
    CHECK(result.noise_figure.status == ReceiverRequirementStatus::Pass);
    CHECK(result.overall == ReceiverRequirementStatus::Fail);
}

TEST_CASE("ReceiverRequirements requires configured sweep endpoints to enclose the band",
          "[receiver_requirements]") {
    const auto state = configuredState();
    const std::vector<double> f = frequencies();
    const std::vector<double> gain(f.size(), 15.0);
    const std::vector<double> nf(f.size(), 3.0);

    const auto result = evaluate(state, 1.01e9, 2.1e9, f, gain, nf);
    CHECK(result.gain.status == ReceiverRequirementStatus::Incomplete);
    CHECK(result.noise_figure.status == ReceiverRequirementStatus::Incomplete);
    CHECK(result.overall == ReceiverRequirementStatus::Incomplete);
}
TEST_CASE("ReceiverRequirements preserves violations in a partly covered band",
          "[receiver_requirements]") {
    const auto state = configuredState();
    const std::vector<double> f = {1.0e9, 1.5e9, 2.0e9};
    const std::vector<double> gain = {15.0, 9.0, 15.0};
    const std::vector<double> nf = {3.0, 3.0, 3.0};

    const auto result = evaluate(state, 1.0e9, 1.5e9, f, gain, nf);
    CHECK(result.gain.status == ReceiverRequirementStatus::Fail);
    CHECK(result.noise_figure.status == ReceiverRequirementStatus::Incomplete);
    CHECK(result.overall == ReceiverRequirementStatus::Fail);
}

TEST_CASE("ReceiverRequirements scans aligned samples in short measurement vectors",
          "[receiver_requirements]") {
    const auto state = configuredState();
    const std::vector<double> f = {1.0e9, 1.5e9, 2.0e9};
    const std::vector<double> gain = {15.0, 9.0};
    const std::vector<double> nf = {3.0, 3.0, 3.0};

    const auto result = evaluate(state, 1.0e9, 2.0e9, f, gain, nf);
    CHECK(result.gain.status == ReceiverRequirementStatus::Fail);
    CHECK(result.noise_figure.status == ReceiverRequirementStatus::Pass);
    CHECK(result.overall == ReceiverRequirementStatus::Fail);
}

TEST_CASE("ReceiverRequirements identifies an invalid present config", "[receiver_requirements]") {
    auto state = configuredState();
    state.config->gain_min_dB = 21.0;

    const auto result = evaluate(state, 0.9e9, 2.1e9, frequencies(), std::vector<double>(5, 15.0),
                                 std::vector<double>(5, 3.0));
    CHECK(result.gain.status == ReceiverRequirementStatus::InvalidConfiguration);
    CHECK(result.noise_figure.status == ReceiverRequirementStatus::InvalidConfiguration);
    CHECK(result.overall == ReceiverRequirementStatus::InvalidConfiguration);
}

TEST_CASE("ReceiverRequirements keeps size mismatch incompleteness metric-specific",
          "[receiver_requirements]") {
    const auto state = configuredState();
    const std::vector<double> f = frequencies();
    const std::vector<double> gain(f.size(), 15.0);
    const std::vector<double> short_nf(f.size() - 1, 3.0);

    const auto result = evaluate(state, 0.9e9, 2.1e9, f, gain, short_nf);
    CHECK(result.gain.status == ReceiverRequirementStatus::Pass);
    CHECK(result.noise_figure.status == ReceiverRequirementStatus::Incomplete);
    CHECK(result.overall == ReceiverRequirementStatus::Incomplete);
}

TEST_CASE("ReceiverRequirements does not pass a band with no discrete in-band samples",
          "[receiver_requirements]") {
    auto state = configuredState();
    state.config->band_start_Hz = 1.25e9;
    state.config->band_stop_Hz = 1.45e9;
    const std::vector<double> f = {0.9e9, 1.0e9, 1.5e9, 2.0e9, 2.1e9};
    const std::vector<double> gain(f.size(), 15.0);
    const std::vector<double> nf(f.size(), 3.0);

    const auto result = evaluate(state, 0.9e9, 2.1e9, f, gain, nf);
    CHECK(result.gain.status == ReceiverRequirementStatus::Incomplete);
    CHECK(result.noise_figure.status == ReceiverRequirementStatus::Incomplete);
    CHECK(result.overall == ReceiverRequirementStatus::Incomplete);
}

TEST_CASE("ReceiverRequirements keeps metric outcomes independent and preserves known failures",
          "[receiver_requirements]") {
    const auto state = configuredState();
    const std::vector<double> f = frequencies();
    std::vector<double> gain = {0.0, 10.0, 15.0, 20.0, 0.0};
    std::vector<double> nf = {0.0, 3.0, std::numeric_limits<double>::quiet_NaN(), 4.0, 0.0};

    auto result = evaluate(state, 0.9e9, 2.1e9, f, gain, nf);
    CHECK(result.gain.status == ReceiverRequirementStatus::Pass);
    CHECK(result.noise_figure.status == ReceiverRequirementStatus::Incomplete);
    CHECK(result.overall == ReceiverRequirementStatus::Incomplete);

    gain[1] = 9.9;
    result = evaluate(state, 0.9e9, 2.1e9, f, gain, nf);
    CHECK(result.gain.status == ReceiverRequirementStatus::Fail);
    CHECK(result.noise_figure.status == ReceiverRequirementStatus::Incomplete);
    CHECK(result.overall == ReceiverRequirementStatus::Fail);
}

TEST_CASE("ReceiverRequirements treats mismatched measurement vectors as incomplete",
          "[receiver_requirements]") {
    const auto state = configuredState();
    const std::vector<double> f = frequencies();
    const std::vector<double> gain(f.size() - 1, 15.0);
    const std::vector<double> nf(f.size(), 3.0);

    const auto result = evaluate(state, 0.9e9, 2.1e9, f, gain, nf);
    CHECK(result.gain.status == ReceiverRequirementStatus::Incomplete);
    CHECK(result.noise_figure.status == ReceiverRequirementStatus::Pass);
    CHECK(result.overall == ReceiverRequirementStatus::Incomplete);
}

TEST_CASE("ReceiverRequirements draft Apply rejects incomplete limits and repairs invalid state",
          "[receiver_requirements]") {
    ReceiverRequirementsState state;
    state.invalid_reason = "malformed persisted requirements";
    const ReceiverRequirementsDraft incomplete{"1e9", "", "10", "20", "5"};
    std::string error;

    CHECK_FALSE(applyReceiverRequirementsDraft(state, incomplete, error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(state.config.has_value());
    CHECK(state.invalid_reason == "malformed persisted requirements");

    const ReceiverRequirementsDraft reversed{"2e9", "1e9", "20", "10", "5"};
    CHECK_FALSE(applyReceiverRequirementsDraft(state, reversed, error));
    CHECK_FALSE(state.config.has_value());
    CHECK_FALSE(state.invalid_reason.empty());

    const ReceiverRequirementsDraft valid{"1e9", "2e9", "10", "20", "5"};
    REQUIRE(applyReceiverRequirementsDraft(state, valid, error));
    REQUIRE(state.config.has_value());
    CHECK(state.invalid_reason.empty());
    CHECK(state.config->band_start_Hz == Approx(1e9));
    CHECK(state.config->band_stop_Hz == Approx(2e9));
    CHECK(state.config->gain_min_dB == Approx(10.0));
    CHECK(state.config->gain_max_dB == Approx(20.0));
    CHECK(state.config->nf_max_dB == Approx(5.0));
}
