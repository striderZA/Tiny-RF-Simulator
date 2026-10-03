#pragma once

#include <optional>
#include <string>
#include <vector>

enum class ReceiverRequirementStatus {
    NotConfigured,
    InvalidConfiguration,
    Incomplete,
    Pass,
    Fail
};

struct ReceiverGainLimits {
    double minimum_dB;
    double maximum_dB;
};

struct ReceiverOutputPowerLimits {
    double minimum_dBm;
    double maximum_dBm;
};

struct ReceiverIIP3TestSettings {
    double tone_spacing_Hz;
    double input_start_dBm;
    double input_stop_dBm;
    double input_step_dB;
};

struct ReceiverMeasurementConditions {
    std::optional<double> output_reference_tone_frequency_Hz;
    std::optional<ReceiverIIP3TestSettings> iip3;
};

struct ReceiverRequirementsConfig {
    double band_start_Hz = 0.0;
    double band_stop_Hz = 0.0;
    std::optional<ReceiverGainLimits> gain;
    std::optional<double> nf_max_dB;
    std::optional<ReceiverOutputPowerLimits> output_power;
    std::optional<double> iip3_min_dBm;
    ReceiverMeasurementConditions measurement_conditions;
};

struct ReceiverRequirementsState {
    std::optional<ReceiverRequirementsConfig> config;
    std::string invalid_reason;
};

struct ReceiverRequirementsDraft {
    std::string band_start_Hz;
    std::string band_stop_Hz;
    std::string gain_min_dB;
    std::string gain_max_dB;
    std::string nf_max_dB;
    std::string output_power_min_dBm;
    std::string output_power_max_dBm;
    std::string iip3_min_dBm;
    std::string iip3_tone_spacing_Hz;
    std::string iip3_input_start_dBm;
    std::string iip3_input_stop_dBm;
    std::string iip3_input_step_dB;
    bool gain_enabled = false;
    bool noise_figure_enabled = false;
    bool output_power_enabled = false;
    bool iip3_enabled = false;
    std::optional<double> output_reference_tone_frequency_Hz;
};

struct ReceiverMetricEvaluation {
    ReceiverRequirementStatus status = ReceiverRequirementStatus::Incomplete;
    std::optional<double> observed_min;
    std::optional<double> observed_max;
};

struct ReceiverRequirementsEvaluation {
    ReceiverMetricEvaluation gain;
    ReceiverMetricEvaluation noise_figure;
    ReceiverMetricEvaluation output_power;
    ReceiverMetricEvaluation iip3;
    ReceiverRequirementStatus overall = ReceiverRequirementStatus::Incomplete;
};

std::optional<ReceiverRequirementsConfig>
parseReceiverRequirementsDraft(const ReceiverRequirementsDraft &draft, std::string &error);
bool applyReceiverRequirementsDraft(ReceiverRequirementsState &state,
                                    const ReceiverRequirementsDraft &draft, std::string &error);
std::optional<std::string>
validateReceiverRequirementsConfig(const ReceiverRequirementsConfig &config);
ReceiverRequirementsEvaluation evaluateReceiverRequirements(
    const ReceiverRequirementsState &state, double sweep_start_Hz, double sweep_stop_Hz,
    const std::vector<double> &frequencies_Hz, const std::vector<double> &gain_dB,
    const std::vector<double> &noise_figure_dB, const std::vector<double> &output_power_dBm,
    const std::vector<double> &iip3_dBm);
