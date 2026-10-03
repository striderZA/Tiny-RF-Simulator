#include "receiver_requirements.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace {
std::optional<double> parseNumber(const std::string &text) {
    if (text.empty())
        return std::nullopt;
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(value))
        return std::nullopt;
    return value;
}

std::optional<std::size_t> iip3LevelCount(const ReceiverIIP3TestSettings &settings) {
    if (!std::isfinite(settings.input_start_dBm) || !std::isfinite(settings.input_stop_dBm) ||
        !std::isfinite(settings.input_step_dB) || settings.input_step_dB <= 0.0 ||
        settings.input_start_dBm >= settings.input_stop_dBm)
        return std::nullopt;

    std::size_t count = 0;
    for (std::size_t n = 0;; ++n) {
        const double level =
            settings.input_start_dBm + static_cast<double>(n) * settings.input_step_dB;
        if (!std::isfinite(level) || level > settings.input_stop_dBm)
            break;
        if (count == std::numeric_limits<std::size_t>::max())
            return std::nullopt;
        ++count;
        if (n == std::numeric_limits<std::size_t>::max())
            return std::nullopt;
    }
    return count;
}

ReceiverMetricEvaluation unavailable(ReceiverRequirementStatus status) { return {status, {}, {}}; }

ReceiverMetricEvaluation assess(const std::vector<double> &frequencies,
                                const std::vector<double> &values, double low_Hz, double high_Hz,
                                bool gain, double minimum, double maximum, bool band_covered) {
    bool any = false;
    bool missing = frequencies.size() != values.size();
    bool failed = false;
    double observed_min = 0.0;
    double observed_max = 0.0;
    const std::size_t aligned_size = std::min(frequencies.size(), values.size());
    for (std::size_t i = 0; i < aligned_size; ++i) {
        const double frequency = frequencies[i];
        if (!std::isfinite(frequency) || frequency < low_Hz || frequency > high_Hz)
            continue;
        if (!std::isfinite(values[i])) {
            missing = true;
            continue;
        }
        const double value = values[i];
        if (!any)
            observed_min = observed_max = value;
        else {
            observed_min = std::min(observed_min, value);
            observed_max = std::max(observed_max, value);
        }
        any = true;
        if ((gain && (value < minimum || value > maximum)) || (!gain && value > maximum))
            failed = true;
    }
    const auto status =
        failed ? ReceiverRequirementStatus::Fail
               : (!band_covered || !any || missing ? ReceiverRequirementStatus::Incomplete
                                                   : ReceiverRequirementStatus::Pass);
    return {status, any ? std::optional<double>(observed_min) : std::nullopt,
            any ? std::optional<double>(observed_max) : std::nullopt};
}

ReceiverMetricEvaluation assessIIP3(const std::vector<double> &values, std::size_t expected_count,
                                    double minimum, bool band_covered) {
    bool missing = values.size() != expected_count;
    bool any = false;
    bool failed = false;
    double observed_min = 0.0;
    double observed_max = 0.0;
    for (std::size_t i = 0; i < std::min(values.size(), expected_count); ++i) {
        if (!std::isfinite(values[i])) {
            missing = true;
            continue;
        }
        if (!any)
            observed_min = observed_max = values[i];
        else {
            observed_min = std::min(observed_min, values[i]);
            observed_max = std::max(observed_max, values[i]);
        }
        any = true;
        failed = failed || values[i] < minimum;
    }
    const auto status =
        failed ? ReceiverRequirementStatus::Fail
               : (!band_covered || !any || missing ? ReceiverRequirementStatus::Incomplete
                                                   : ReceiverRequirementStatus::Pass);
    return {status, any ? std::optional<double>(observed_min) : std::nullopt,
            any ? std::optional<double>(observed_max) : std::nullopt};
}

ReceiverRequirementStatus aggregate(const ReceiverRequirementsEvaluation &result) {
    const ReceiverMetricEvaluation *metrics[] = {&result.gain, &result.noise_figure,
                                                 &result.output_power, &result.iip3};
    bool any_enabled = false;
    bool all_pass = true;
    for (const auto *metric : metrics) {
        if (metric->status == ReceiverRequirementStatus::NotConfigured)
            continue;
        any_enabled = true;
        if (metric->status == ReceiverRequirementStatus::Fail)
            return ReceiverRequirementStatus::Fail;
        all_pass = all_pass && metric->status == ReceiverRequirementStatus::Pass;
    }
    if (!any_enabled)
        return ReceiverRequirementStatus::NotConfigured;
    return all_pass ? ReceiverRequirementStatus::Pass : ReceiverRequirementStatus::Incomplete;
}
} // namespace

std::optional<std::string>
validateReceiverRequirementsConfig(const ReceiverRequirementsConfig &config) {
    if (!std::isfinite(config.band_start_Hz) || !std::isfinite(config.band_stop_Hz))
        return "Requirement band endpoints must be finite.";
    if (config.band_start_Hz >= config.band_stop_Hz)
        return "Requirement band start must be below band stop.";
    if (config.gain &&
        (!std::isfinite(config.gain->minimum_dB) || !std::isfinite(config.gain->maximum_dB)))
        return "Gain limits must be finite.";
    if (config.gain && config.gain->minimum_dB > config.gain->maximum_dB)
        return "Minimum gain must not exceed maximum gain.";
    if (config.nf_max_dB && !std::isfinite(*config.nf_max_dB))
        return "Noise figure limit must be finite.";
    if (config.output_power && (!std::isfinite(config.output_power->minimum_dBm) ||
                                !std::isfinite(config.output_power->maximum_dBm)))
        return "Output power limits must be finite.";
    if (config.output_power && config.output_power->minimum_dBm > config.output_power->maximum_dBm)
        return "Minimum output power must not exceed maximum output power.";
    if (config.iip3_min_dBm) {
        if (!std::isfinite(*config.iip3_min_dBm))
            return "IIP3 limit must be finite.";
        if (!config.measurement_conditions.iip3)
            return "IIP3 test settings are required when the IIP3 metric is enabled.";
        const auto &settings = *config.measurement_conditions.iip3;
        if (!std::isfinite(settings.tone_spacing_Hz) || settings.tone_spacing_Hz <= 0.0)
            return "IIP3 tone spacing must be finite and positive.";
        const auto count = iip3LevelCount(settings);
        if (!count)
            return "IIP3 input sweep must be finite, ordered, and have a positive step.";
        if (*count < 3)
            return "IIP3 input sweep must generate at least three levels.";
    }
    if (config.measurement_conditions.output_reference_tone_frequency_Hz &&
        (!std::isfinite(*config.measurement_conditions.output_reference_tone_frequency_Hz) ||
         *config.measurement_conditions.output_reference_tone_frequency_Hz <= 0.0))
        return "Output reference tone frequency must be finite and positive.";
    return std::nullopt;
}

std::optional<ReceiverRequirementsConfig>
parseReceiverRequirementsDraft(const ReceiverRequirementsDraft &draft, std::string &error) {
    const auto band_start = parseNumber(draft.band_start_Hz);
    const auto band_stop = parseNumber(draft.band_stop_Hz);
    if (!band_start || !band_stop) {
        error = "Requirement band endpoints must be finite numbers.";
        return std::nullopt;
    }
    ReceiverRequirementsConfig config;
    config.band_start_Hz = *band_start;
    config.band_stop_Hz = *band_stop;

    auto required = [&](const std::string &text, const char *label) -> std::optional<double> {
        auto value = parseNumber(text);
        if (!value)
            error = std::string(label) + " must be a finite number.";
        return value;
    };
    if (draft.gain_enabled) {
        auto minimum = required(draft.gain_min_dB, "Gain minimum");
        auto maximum = required(draft.gain_max_dB, "Gain maximum");
        if (!minimum || !maximum)
            return std::nullopt;
        config.gain = ReceiverGainLimits{*minimum, *maximum};
    }
    if (draft.noise_figure_enabled) {
        auto maximum = required(draft.nf_max_dB, "Noise figure maximum");
        if (!maximum)
            return std::nullopt;
        config.nf_max_dB = *maximum;
    }
    if (draft.output_power_enabled) {
        auto minimum = required(draft.output_power_min_dBm, "Output power minimum");
        auto maximum = required(draft.output_power_max_dBm, "Output power maximum");
        if (!minimum || !maximum)
            return std::nullopt;
        config.output_power = ReceiverOutputPowerLimits{*minimum, *maximum};
    }
    if (draft.iip3_enabled) {
        auto minimum = required(draft.iip3_min_dBm, "IIP3 minimum");
        auto spacing = required(draft.iip3_tone_spacing_Hz, "IIP3 tone spacing");
        auto start = required(draft.iip3_input_start_dBm, "IIP3 input start");
        auto stop = required(draft.iip3_input_stop_dBm, "IIP3 input stop");
        auto step = required(draft.iip3_input_step_dB, "IIP3 input step");
        if (!minimum || !spacing || !start || !stop || !step)
            return std::nullopt;
        config.iip3_min_dBm = *minimum;
        config.measurement_conditions.iip3 =
            ReceiverIIP3TestSettings{*spacing, *start, *stop, *step};
    }
    config.measurement_conditions.output_reference_tone_frequency_Hz =
        draft.output_reference_tone_frequency_Hz;
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
    if (!config->gain && !config->nf_max_dB && !config->output_power && !config->iip3_min_dBm)
        state.config.reset();
    else
        state.config = *config;
    state.invalid_reason.clear();
    return true;
}

ReceiverRequirementsEvaluation evaluateReceiverRequirements(
    const ReceiverRequirementsState &state, double sweep_start_Hz, double sweep_stop_Hz,
    const std::vector<double> &frequencies_Hz, const std::vector<double> &gain_dB,
    const std::vector<double> &noise_figure_dB, const std::vector<double> &output_power_dBm,
    const std::vector<double> &iip3_dBm) {
    ReceiverRequirementsEvaluation result;
    if (!state.invalid_reason.empty()) {
        result.gain = result.noise_figure = result.output_power = result.iip3 =
            unavailable(ReceiverRequirementStatus::InvalidConfiguration);
        result.overall = ReceiverRequirementStatus::InvalidConfiguration;
        return result;
    }
    if (!state.config) {
        result.gain = result.noise_figure = result.output_power = result.iip3 =
            unavailable(ReceiverRequirementStatus::NotConfigured);
        result.overall = ReceiverRequirementStatus::NotConfigured;
        return result;
    }
    const auto &config = *state.config;
    if (auto reason = validateReceiverRequirementsConfig(config)) {
        result.gain = result.noise_figure = result.output_power = result.iip3 =
            unavailable(ReceiverRequirementStatus::InvalidConfiguration);
        result.overall = ReceiverRequirementStatus::InvalidConfiguration;
        return result;
    }
    result.gain = config.gain ? unavailable(ReceiverRequirementStatus::Incomplete)
                              : unavailable(ReceiverRequirementStatus::NotConfigured);
    result.noise_figure = config.nf_max_dB ? unavailable(ReceiverRequirementStatus::Incomplete)
                                           : unavailable(ReceiverRequirementStatus::NotConfigured);
    result.output_power = config.output_power
                              ? unavailable(ReceiverRequirementStatus::Incomplete)
                              : unavailable(ReceiverRequirementStatus::NotConfigured);
    result.iip3 = config.iip3_min_dBm ? unavailable(ReceiverRequirementStatus::Incomplete)
                                      : unavailable(ReceiverRequirementStatus::NotConfigured);

    const bool sweep_valid = std::isfinite(sweep_start_Hz) && std::isfinite(sweep_stop_Hz) &&
                             sweep_start_Hz <= sweep_stop_Hz;
    const bool band_covered = sweep_valid && sweep_start_Hz <= config.band_start_Hz &&
                              sweep_stop_Hz >= config.band_stop_Hz;
    if (config.gain)
        result.gain = assess(frequencies_Hz, gain_dB, config.band_start_Hz, config.band_stop_Hz,
                             true, config.gain->minimum_dB, config.gain->maximum_dB, band_covered);
    if (config.nf_max_dB)
        result.noise_figure =
            assess(frequencies_Hz, noise_figure_dB, config.band_start_Hz, config.band_stop_Hz,
                   false, 0.0, *config.nf_max_dB, band_covered);
    if (config.output_power) {
        const auto tone = config.measurement_conditions.output_reference_tone_frequency_Hz;
        if (tone && *tone >= config.band_start_Hz && *tone <= config.band_stop_Hz)
            result.output_power = assess(
                frequencies_Hz, output_power_dBm, config.band_start_Hz, config.band_stop_Hz, true,
                config.output_power->minimum_dBm, config.output_power->maximum_dBm, band_covered);
    }
    if (config.iip3_min_dBm && config.measurement_conditions.iip3) {
        const auto count = iip3LevelCount(*config.measurement_conditions.iip3);
        if (count)
            result.iip3 = assessIIP3(iip3_dBm, *count, *config.iip3_min_dBm, band_covered);
    }
    result.overall = aggregate(result);
    return result;
}
