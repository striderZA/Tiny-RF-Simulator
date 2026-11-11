#pragma once

#include "receiver_requirements.h"
#include "rf_budget.h"
#include "rf_budget_stage.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ComponentLibrary;

// One stage's contribution to one path, on one frequency grid. The first four series are the
// stage's own response; the rest are the cascade through it. NaN marks no value and carries the
// reason; +/-infinity is a real value (no transmission, linear OIP3, or infinite headroom).
struct RfBudgetStageResult {
    std::string stage;
    int input_port = 0;
    int output_port = 0;
    RfBudgetSeries gain_dB;
    RfBudgetSeries nf_dB;
    RfBudgetSeries oip3_dBm;
    RfBudgetSeries cumulative_gain_dB;
    RfBudgetSeries cumulative_nf_dB;
    RfBudgetSeries cumulative_iip3_dBm; // input-referred
    RfBudgetSeries level_dBm;
    RfBudgetSeries nf_share_percent;
    RfBudgetSeries p1db_headroom_dB;
};

// One entered stage on a route: its evaluated response and the pins the signal uses.
struct RfBudgetPathHop {
    const RfBudgetStageResponse *response = nullptr;
    int input_port = 0;
    int output_port = 0;
};

// Friis, reciprocal IIP3, level, NF share, and headroom per point, in hop order. `mode_name`
// names the mode in the missing-input-level reason; `reasons` interns every refusal sentence.
std::vector<RfBudgetStageResult> cascadeRfBudgetPath(const std::vector<RfBudgetPathHop> &hops,
                                                     std::optional<double> input_level_dBm,
                                                     std::string_view mode_name,
                                                     RfBudgetReasonTable &reasons);

// Blanks the readouts of each point whose finite cumulative gain is below the Network Analyzer's
// -100 dB floor: cumulative gain, cumulative NF, level, and headroom. A value that is already
// blank keeps its reason. The running sums and IIP3 keep the true gain. Call it after the verdict,
// which judges the unfloored values.
void floorRfBudgetReadouts(std::vector<RfBudgetStageResult> &stages, RfBudgetReasonTable &reasons);

// One output a mode reaches, and the requirements verdict for its cascade totals.
struct RfBudgetCascade {
    std::string output;
    std::vector<double> frequencies_Hz;
    std::vector<RfBudgetStageResult> stages;
    ReceiverRequirementsEvaluation verdict;
};

struct RfBudgetUnreachedOutput {
    std::string output;
    std::string reason;
};

enum class RfBudgetModeStatus { Budgeted, NotBudgeted, NotConfigured };

struct RfBudgetModeResult {
    std::uint32_t mode_id = 0;
    std::string mode_name;
    RfBudgetModeStatus status = RfBudgetModeStatus::NotBudgeted;
    std::vector<RfBudgetCascade> cascades;
    std::vector<RfBudgetUnreachedOutput> unreached;
};

struct RfBudgetEvaluation {
    std::string not_evaluated_reason;
    std::vector<RfBudgetModeResult> modes; // requirements order
    std::vector<std::uint32_t> orphaned_mode_ids;
    RfBudgetReasonTable reasons;
};

// Routes every requirements mode through its budget record's stage settings and evaluates the
// cascade and verdict of each reached output. One stage evaluator serves the whole call. With
// `only_mode_id`, only that mode is evaluated; orphans are always reported against the whole set.
RfBudgetEvaluation evaluateRfBudget(const RfBudget &budget,
                                    const ReceiverRequirementsSet &requirements,
                                    const ComponentLibrary &library,
                                    std::optional<std::uint32_t> only_mode_id = std::nullopt);

// The evaluated mode with `mode_id`, or nullptr when the evaluation does not contain it.
const RfBudgetModeResult *findRfBudgetModeResult(const RfBudgetEvaluation &evaluation,
                                                 std::uint32_t mode_id);
