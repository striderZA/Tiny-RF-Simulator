#include "receiver_requirements.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

std::optional<std::string>
validateReceiverRequirementsConfig(const ReceiverRequirementsConfig &config) {
    if (!std::isfinite(config.band_start_Hz) || !std::isfinite(config.band_stop_Hz) ||
        !std::isfinite(config.gain_min_dB) || !std::isfinite(config.gain_max_dB) ||
        !std::isfinite(config.nf_max_dB)) {
        return "All requirement limits must be finite.";
    }
    if (config.band_start_Hz >= config.band_stop_Hz)
        return "Requirement band start must be below band stop.";
    if (config.gain_min_dB > config.gain_max_dB)
        return "Minimum gain must not exceed maximum gain.";
    return std::nullopt;
}

std::optional<ReceiverRequirementsConfig>
parseReceiverRequirementsDraft(const ReceiverRequirementsDraft &draft, std::string &error) {
    double values[5]{};
    for (std::size_t i = 0; i < draft.size(); ++i) {
        if (draft[i].empty()) {
            error = "All five requirement values are required.";
            return std::nullopt;
        }
        char *end = nullptr;
        values[i] = std::strtod(draft[i].c_str(), &end);
        if (end == draft[i].c_str() || *end != '\0' || !std::isfinite(values[i])) {
            error = "Requirement values must be finite numbers.";
            return std::nullopt;
        }
    }
    ReceiverRequirementsConfig config{values[0], values[1], values[2], values[3], values[4]};
    if (auto reason = validateReceiverRequirementsConfig(config)) {
        error = *reason;
        return std::nullopt;
    }
    error.clear();
    return config;
}

bool applyReceiverRequirementsDraft(ReceiverRequirementsState &state,
                                    const ReceiverRequirementsDraft &draft, std::string &error) {
    auto config = parseReceiverRequirementsDraft(draft, error);
    if (!config)
        return false;
    state.config = *config;
    state.invalid_reason.clear();
    return true;
}

namespace {
ReceiverMetricEvaluation unavailable(ReceiverRequirementStatus status) { return {status, {}, {}}; }

ReceiverMetricEvaluation assess(const std::vector<double> &frequencies,
                                const std::vector<double> &values, double low_Hz, double high_Hz,
                                bool gain, double min_dB, double max_dB, bool band_covered) {
    bool any = false, missing = frequencies.size() != values.size(), failed = false;
    double observed_min = 0.0, observed_max = 0.0;
    const std::size_t aligned_size = std::min(frequencies.size(), values.size());
    for (std::size_t i = 0; i < aligned_size; ++i) {
        const double f = frequencies[i];
        if (!std::isfinite(f) || f < low_Hz || f > high_Hz)
            continue;
        if (!std::isfinite(values[i])) {
            missing = true;
            continue;
        }
        const double v = values[i];
        if (!any)
            observed_min = observed_max = v;
        else {
            observed_min = std::min(observed_min, v);
            observed_max = std::max(observed_max, v);
        }
        any = true;
        if (gain ? (v < min_dB || v > max_dB) : (v > max_dB))
            failed = true;
    }
    ReceiverMetricEvaluation result;
    result.observed_min_dB = any ? std::optional<double>(observed_min) : std::nullopt;
    result.observed_max_dB = any ? std::optional<double>(observed_max) : std::nullopt;
    result.status = failed
                        ? ReceiverRequirementStatus::Fail
                        : (!band_covered || !any || missing ? ReceiverRequirementStatus::Incomplete
                                                            : ReceiverRequirementStatus::Pass);
    return result;
}
} // namespace

ReceiverRequirementsEvaluation
evaluateReceiverRequirements(const ReceiverRequirementsState &state, double sweep_start_Hz,
                             double sweep_stop_Hz, const std::vector<double> &frequencies_Hz,
                             const std::vector<double> &gain_dB,
                             const std::vector<double> &noise_figure_dB) {
    ReceiverRequirementsEvaluation result;
    if (!state.invalid_reason.empty()) {
        result.gain = result.noise_figure =
            unavailable(ReceiverRequirementStatus::InvalidConfiguration);
        result.overall = ReceiverRequirementStatus::InvalidConfiguration;
        return result;
    }
    if (!state.config) {
        result.gain = result.noise_figure = unavailable(ReceiverRequirementStatus::NotConfigured);
        result.overall = ReceiverRequirementStatus::NotConfigured;
        return result;
    }
    const auto &c = *state.config;
    if (validateReceiverRequirementsConfig(c)) {
        result.gain = result.noise_figure =
            unavailable(ReceiverRequirementStatus::InvalidConfiguration);
        result.overall = ReceiverRequirementStatus::InvalidConfiguration;
        return result;
    }
    const bool sweep_valid = std::isfinite(sweep_start_Hz) && std::isfinite(sweep_stop_Hz) &&
                             sweep_start_Hz <= sweep_stop_Hz;
    const bool band_covered =
        sweep_valid && sweep_start_Hz <= c.band_start_Hz && sweep_stop_Hz >= c.band_stop_Hz;
    result.gain = assess(frequencies_Hz, gain_dB, c.band_start_Hz, c.band_stop_Hz, true,
                         c.gain_min_dB, c.gain_max_dB, band_covered);
    result.noise_figure = assess(frequencies_Hz, noise_figure_dB, c.band_start_Hz, c.band_stop_Hz,
                                 false, 0.0, c.nf_max_dB, band_covered);
    const auto g = result.gain.status, n = result.noise_figure.status;
    result.overall =
        (g == ReceiverRequirementStatus::Fail || n == ReceiverRequirementStatus::Fail)
            ? ReceiverRequirementStatus::Fail
            : (g == ReceiverRequirementStatus::Pass && n == ReceiverRequirementStatus::Pass
                   ? ReceiverRequirementStatus::Pass
                   : ReceiverRequirementStatus::Incomplete);
    return result;
}
