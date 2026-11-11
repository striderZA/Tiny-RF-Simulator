#pragma once

#include "receiver_requirements.h"
#include "rf_budget_cascade.h"
#include <array>
#include <cstddef>
#include <functional>
#include <string>

enum class ReceiverRequirementStatusTone { Neutral, PassGreen, FailRed };

ReceiverRequirementStatusTone receiverRequirementStatusTone(ReceiverRequirementStatus status);

class ReceiverRequirementsWidget {
  public:
    void draw(const char *title, bool *p_open, ReceiverRequirementsSet &set,
              const ReceiverRequirementsEvaluation &result,
              const RfBudgetEvaluation &budget = RfBudgetEvaluation{});

    std::function<void()> onChange;
    // The validation error shown under the mode controls; empty when there is none.
    const std::string &error() const { return m_error; }

  private:
    void resetDraft(const ReceiverRequirementsSet &set);
    void fillModeName(const ReceiverRequirementsSet &set);
    ReceiverRequirementsDraft makeDraft() const;
    ReceiverRequirementsDraft m_baseline;
    std::array<std::array<char, 128>, 13> m_buffers{};
    std::array<char, 128> m_mode_name{};
    ReceiverRequirementsSet m_snapshot;
    std::size_t m_name_selected = 0;
    bool m_gain_enabled = false;
    bool m_nf_enabled = false;
    bool m_output_power_enabled = false;
    bool m_iip3_enabled = false;
    bool m_initialized = false;
    std::string m_error;
    std::string m_notice;
    std::uint32_t m_remove_id = 0; // the mode the open Remove popup would delete
};
