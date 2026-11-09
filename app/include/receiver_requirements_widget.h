#pragma once

#include "receiver_requirements.h"
#include <array>
#include <cstddef>
#include <functional>
#include <string>

enum class ReceiverRequirementStatusTone { Neutral, PassGreen, FailRed };

ReceiverRequirementStatusTone receiverRequirementStatusTone(ReceiverRequirementStatus status);

class ReceiverRequirementsWidget {
  public:
    void draw(const char *title, bool *p_open, ReceiverRequirementsSet &set,
              const ReceiverRequirementsEvaluation &result);

    std::function<void()> onChange;

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
};
