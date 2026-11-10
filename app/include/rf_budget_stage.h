#pragma once

#include "component_params.h"
#include "rf_budget.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class ComponentLibrary;

// Interned refusal sentences for one budget evaluation. Index 0 is the empty string, which marks
// an ordinary value in every series.
class RfBudgetReasonTable {
  public:
    std::uint16_t add(const std::string &reason);
    const std::string &at(std::uint16_t i) const;
    const std::vector<std::string> &all() const { return m_reasons; }

  private:
    std::vector<std::string> m_reasons{""};
};

struct RfBudgetSeries {
    std::vector<double> values;         // NaN: no value; +/-inf allowed
    std::vector<std::uint16_t> reasons; // 0: an ordinary value
};

// One stage's response on a frequency grid. `stage_reason` is non-zero when the stage was not
// built; then every series entry is NaN carrying that reason.
struct RfBudgetStageResponse {
    std::string stage;
    std::uint16_t stage_reason = 0;
    std::vector<RfBudgetSeries> gain_dB, nf_dB; // per output port
    RfBudgetSeries oip3_dBm;
    int active_throw = -1;
};

// Builds each stage as a real engine in a private scratch graph and drives it with the Network
// Analyzer's own comb (sweepStimulus at -30 dBm on one input port). Engines are cached by stage
// name and overrides, so one evaluator serves every mode and output of a budget.
class RfBudgetStageEvaluator {
  public:
    RfBudgetStageEvaluator(const ComponentLibrary &library, RfBudgetReasonTable &reasons);
    ~RfBudgetStageEvaluator();
    RfBudgetStageEvaluator(const RfBudgetStageEvaluator &) = delete;
    RfBudgetStageEvaluator &operator=(const RfBudgetStageEvaluator &) = delete;

    RfBudgetStageResponse evaluate(const RfBudgetStage &stage,
                                   const std::map<std::string, double> *overrides,
                                   const std::vector<double> &frequencies_Hz, int input_port);

  private:
    struct CacheEntry;
    std::unique_ptr<CacheEntry> buildEntry(const RfBudgetStage &stage,
                                           const std::map<std::string, double> &overrides);
    const ComponentLibrary &m_library;
    RfBudgetReasonTable &m_reasons;
    std::vector<std::unique_ptr<CacheEntry>> m_cache;
};

struct RfBudgetParamIssue {
    std::string path; // JSON pointer
    ParamWriteResult result;
};

// Idealized params, then each mode record's overrides, written to a scratch engine of the type
// through applyComponentParams; library-stage overrides use a default engine of the type. Returns
// the first refusal, or nullopt.
std::optional<RfBudgetParamIssue> checkRfBudgetParams(const RfBudget &budget);
