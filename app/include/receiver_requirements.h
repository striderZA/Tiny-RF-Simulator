#pragma once

#include <cstddef>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
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
    bool operator==(const ReceiverGainLimits &) const = default;
};

struct ReceiverOutputPowerLimits {
    double minimum_dBm;
    double maximum_dBm;
    bool operator==(const ReceiverOutputPowerLimits &) const = default;
};

struct ReceiverIIP3TestSettings {
    double tone_spacing_Hz;
    double input_start_dBm;
    double input_stop_dBm;
    double input_step_dB;
    bool operator==(const ReceiverIIP3TestSettings &) const = default;
};

constexpr std::size_t kMaxReceiverIIP3InputLevels = 101;

std::optional<std::size_t> receiverIIP3LevelCount(const ReceiverIIP3TestSettings &settings);

struct ReceiverMeasurementConditions {
    std::optional<double> input_level_dBm;
    std::optional<ReceiverIIP3TestSettings> iip3;
    bool operator==(const ReceiverMeasurementConditions &) const = default;
};

struct ReceiverRequirementsConfig {
    double band_start_Hz = 0.0;
    double band_stop_Hz = 0.0;
    std::optional<ReceiverGainLimits> gain;
    std::optional<double> nf_max_dB;
    std::optional<ReceiverOutputPowerLimits> output_power;
    std::optional<double> iip3_min_dBm;
    ReceiverMeasurementConditions measurement_conditions;
    bool operator==(const ReceiverRequirementsConfig &) const = default;
};

constexpr std::size_t kMaxReceiverRequirementModes = 64;
constexpr std::size_t kMaxReceiverRequirementModeNameLength = 64;

struct ReceiverRequirementsMode {
    std::string name;
    std::optional<ReceiverRequirementsConfig> config; // nullopt = not configured
    bool operator==(const ReceiverRequirementsMode &) const = default;
};

struct ReceiverRequirementsSet {
    std::vector<ReceiverRequirementsMode> modes;
    std::size_t selected = 0;   // 0 when modes is empty
    std::string invalid_reason; // set only for retained malformed project data; modes empty
    bool operator==(const ReceiverRequirementsSet &) const = default;
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
    std::string input_level_dBm;
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
std::optional<std::string>
receiverRequirementsModeNameError(const ReceiverRequirementsSet &set, std::string_view trimmed_name,
                                  std::optional<std::size_t> except_index);
std::optional<std::string> validateReceiverRequirementsSet(const ReceiverRequirementsSet &set);

constexpr int kReceiverRequirementsFormat = 2;
nlohmann::json receiverRequirementsToJson(const ReceiverRequirementsSet &set);
ReceiverRequirementsSet receiverRequirementsFromJson(const nlohmann::json &value);
bool addReceiverRequirementsMode(ReceiverRequirementsSet &set, std::string_view name,
                                 std::string &error);
bool renameReceiverRequirementsMode(ReceiverRequirementsSet &set, std::size_t index,
                                    std::string_view name, std::string &error);
bool removeReceiverRequirementsMode(ReceiverRequirementsSet &set, std::size_t index);
bool selectReceiverRequirementsMode(ReceiverRequirementsSet &set, std::size_t index);
bool applyReceiverRequirementsDraftToSelectedMode(ReceiverRequirementsSet &set,
                                                  const ReceiverRequirementsDraft &draft,
                                                  std::string &error);
ReceiverRequirementsState selectedReceiverRequirementsState(const ReceiverRequirementsSet &set);
ReceiverRequirementsEvaluation evaluateReceiverRequirements(
    const ReceiverRequirementsState &state, double sweep_start_Hz, double sweep_stop_Hz,
    const std::vector<double> &frequencies_Hz, const std::vector<double> &gain_dB,
    const std::vector<double> &noise_figure_dB, const std::vector<double> &output_power_dBm,
    const std::vector<double> &iip3_dBm);
