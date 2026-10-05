#pragma once

#include "receiver_requirements.h"
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

enum class ReceiverRequirementStatusTone { Neutral, PassGreen, FailRed };

struct ReceiverGeneratorToneOption {
    double frequency_Hz;
    double power_dBm;
};

ReceiverRequirementStatusTone receiverRequirementStatusTone(ReceiverRequirementStatus status);

class ReceiverRequirementsWidget {
  public:
    void draw(const char *title, bool *p_open, ReceiverRequirementsState &state,
              const ReceiverRequirementsEvaluation &result,
              const std::vector<ReceiverGeneratorToneOption> &source_tones);

    std::function<void()> onChange;

  private:
    void resetDraft(const ReceiverRequirementsState &state,
                    const std::vector<ReceiverGeneratorToneOption> &source_tones);
    bool stateMatchesSnapshot(const ReceiverRequirementsState &state) const;
    ReceiverRequirementsDraft makeDraft() const;
    ReceiverRequirementsDraft m_baseline;
    std::array<std::array<char, 128>, 12> m_buffers{};
    ReceiverRequirementsState m_snapshot;
    std::optional<double> m_source_tone_frequency_Hz;
    bool m_gain_enabled = false;
    bool m_nf_enabled = false;
    bool m_output_power_enabled = false;
    bool m_iip3_enabled = false;
    bool m_initialized = false;
    std::string m_error;
};
