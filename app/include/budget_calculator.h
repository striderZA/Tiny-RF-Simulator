#pragma once

#include "component_library.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// Stage snapshot stored in the project's budget. It is self-contained: the library is
// only consulted by the picker, so a later library edit cannot change a saved budget.
struct BudgetStage {
    std::string type; // library definition type, e.g. "amplifier"
    std::string part_number;
    std::string manufacturer;
    std::string description;
    nlohmann::json parameters = nlohmann::json::object();
    std::vector<DataFileRef> data_files;
    std::string source_path; // library JSON path, for resolving data files
    bool unresolved = false; // set at load time; never saved as true
    std::string unresolved_reason;
};

struct BudgetConfig {
    std::vector<BudgetStage> stages; // cascade order, first stage first
    double input_power_dBm = -30.0;  // output-power prediction
};

// Per-stage values extracted at one frequency grid point.
struct BudgetStageMetrics {
    std::vector<double> gain_dB;
    std::vector<double> nf_dB;
    std::vector<double> oip3_dBm; // +infinity when the stage has no nonlinearity
    std::vector<double> p1db_dBm; // output-referred; +infinity when there is no nonlinearity
    std::vector<bool> out_of_band;
};

struct BudgetResult {
    std::vector<double> gain_dB;
    std::vector<double> nf_dB;
    std::vector<double> output_power_dBm;
    std::vector<double> iip3_dBm; // input-referred
    std::vector<double> p1db_dBm; // output-referred
    std::vector<bool> valid;
};

// Cascades the stage metrics over the shared frequency grid. A point is invalid when any
// stage is out of band, its gain or noise figure is non-finite, or its linear gain is not
// strictly positive; invalid points carry NaN curves and `valid == false`.
//
// Assumptions: matched 50 ohm ports, unilateral stages, a 290 K source. The P1dB cascade is
// the standard small-signal reciprocal approximation, not a substitute for simulation.
BudgetResult computeBudget(const BudgetConfig &config, const std::vector<double> &frequencies_Hz,
                           const std::vector<BudgetStageMetrics> &stage_metrics);

// Project JSON (top-level key `budget_calculator`). `budgetFromJson` never throws on
// malformed input: it keeps the stages it can restore, appends one diagnostic per
// rejected stage, and reports a structural problem in `diagnostic`.
nlohmann::json budgetToJson(const BudgetConfig &config);
BudgetConfig budgetFromJson(const nlohmann::json &json, std::string &diagnostic);
