#pragma once

#include "receiver_requirements.h"
#include "rf_budget_cascade.h"
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

enum class ReceiverRequirementStatusTone { Neutral, PassGreen, FailRed };

ReceiverRequirementStatusTone receiverRequirementStatusTone(ReceiverRequirementStatus status);

class ReceiverRequirementsWidget {
  public:
    void draw(const char *title, bool *p_open, ReceiverRequirementsSet &set,
              const ReceiverRequirementsEvaluation &result,
              const RfBudgetEvaluation &budget = RfBudgetEvaluation{});

    std::function<void()> onChange;
    // The last Apply refusal, shown next to the Apply button; empty when there is none.
    const std::string &error() const { return m_error; }

  private:
    struct OobRow {
        std::array<char, 128> offset{};
        std::array<char, 128> min{};
        int anchor = 0;
    };

    void resetDraft(const ReceiverRequirementsSet &set);
    void fillModeName(const ReceiverRequirementsSet &set);
    ReceiverRequirementsDraft makeDraft() const;
    ReceiverRequirementsDraft m_baseline;
    std::array<std::array<char, 128>, 13> m_buffers{};
    std::array<char, 128> m_mode_name{};
    std::vector<OobRow> m_oob_rows;
    ReceiverRequirementsSet m_snapshot;
    std::size_t m_name_selected = 0;
    bool m_gain_enabled = false;
    bool m_nf_enabled = false;
    bool m_output_power_enabled = false;
    bool m_iip3_enabled = false;
    bool m_initialized = false;
    std::string m_error;      // the last Apply refusal, shown next to Apply
    std::string m_mode_error; // the last Add/Rename/Remove refusal, shown under the mode controls
    std::string m_notice;
    std::uint32_t m_remove_id = 0; // the mode the open Remove popup would delete
};
