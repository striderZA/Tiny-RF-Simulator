#pragma once

#include <array>
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

struct ReceiverRequirementsConfig {
    double band_start_Hz;
    double band_stop_Hz;
    double gain_min_dB;
    double gain_max_dB;
    double nf_max_dB;
};

struct ReceiverRequirementsState {
    std::optional<ReceiverRequirementsConfig> config;
    std::string invalid_reason;
};

using ReceiverRequirementsDraft = std::array<std::string, 5>;

struct ReceiverMetricEvaluation {
    ReceiverRequirementStatus status = ReceiverRequirementStatus::Incomplete;
    std::optional<double> observed_min_dB;
    std::optional<double> observed_max_dB;
};

struct ReceiverRequirementsEvaluation {
    ReceiverMetricEvaluation gain;
    ReceiverMetricEvaluation noise_figure;
    ReceiverRequirementStatus overall = ReceiverRequirementStatus::Incomplete;
};

std::optional<ReceiverRequirementsConfig>
parseReceiverRequirementsDraft(const ReceiverRequirementsDraft &draft, std::string &error);
bool applyReceiverRequirementsDraft(ReceiverRequirementsState &state,
                                    const ReceiverRequirementsDraft &draft, std::string &error);
std::optional<std::string>
validateReceiverRequirementsConfig(const ReceiverRequirementsConfig &config);
ReceiverRequirementsEvaluation
evaluateReceiverRequirements(const ReceiverRequirementsState &state, double sweep_start_Hz,
                             double sweep_stop_Hz, const std::vector<double> &frequencies_Hz,
                             const std::vector<double> &gain_dB,
                             const std::vector<double> &noise_figure_dB);
