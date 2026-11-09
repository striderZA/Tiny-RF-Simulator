#pragma once

#include "receiver_requirements.h"
#include <array>
#include <functional>
#include <string>

enum class ReceiverRequirementStatusTone { Neutral, PassGreen, FailRed };

ReceiverRequirementStatusTone receiverRequirementStatusTone(ReceiverRequirementStatus status);

class ReceiverRequirementsWidget {
  public:
    void draw(const char *title, bool *p_open, ReceiverRequirementsState &state,
              const ReceiverRequirementsEvaluation &result);

    std::function<void()> onChange;

  private:
    void resetDraft(const ReceiverRequirementsState &state);
    bool stateMatchesSnapshot(const ReceiverRequirementsState &state) const;
    ReceiverRequirementsDraft makeDraft() const;
    ReceiverRequirementsDraft m_baseline;
    std::array<std::array<char, 128>, 13> m_buffers{};
    ReceiverRequirementsState m_snapshot;
    bool m_gain_enabled = false;
    bool m_nf_enabled = false;
    bool m_output_power_enabled = false;
    bool m_iip3_enabled = false;
    bool m_initialized = false;
    std::string m_error;
};
