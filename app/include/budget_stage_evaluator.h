#pragma once

#include "budget_calculator.h"

#include <string>
#include <vector>

// Result of evaluating one budget stage over the frequency grid. An unresolved stage keeps
// `resolved == false`, carries its diagnostic, and leaves every metric point NaN.
struct BudgetStageEvaluation {
    BudgetStageMetrics metrics;
    bool resolved = true;
    std::string diagnostic;
};

// Evaluates budget stages on a private scratch runtime, so the live circuit is never
// touched. Each stage goes through ComponentLibrary::instantiate() and therefore the same
// parameter and data-file loading path as a placed component, with one tone at each grid
// frequency. Results are cached and recomputed only when the stage snapshots, the stimulus
// power, or the frequency grid change.
class BudgetStageEvaluator {
  public:
    std::vector<BudgetStageEvaluation> evaluate(const BudgetConfig &config,
                                                ComponentLibrary &library,
                                                const std::vector<double> &frequencies_Hz,
                                                double stimulus_power_dBm);
    void clearCache();

  private:
    std::string m_cache_key;
    std::vector<BudgetStageEvaluation> m_cache;
};
