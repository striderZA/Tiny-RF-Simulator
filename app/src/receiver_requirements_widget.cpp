#include "receiver_requirements_widget.h"
#include "imgui.h"
#include <cstdio>
#include <limits>
#include <string>

namespace {
constexpr const char *kLabels[] = {"Band start (Hz)", "Band stop (Hz)", "Gain min (dB)",
                                   "Gain max (dB)", "NF max (dB)"};

const char *statusText(ReceiverRequirementStatus status) {
    switch (status) {
    case ReceiverRequirementStatus::NotConfigured:
        return "Not configured";
    case ReceiverRequirementStatus::InvalidConfiguration:
        return "Invalid configuration";
    case ReceiverRequirementStatus::Incomplete:
        return "Incomplete";
    case ReceiverRequirementStatus::Pass:
        return "Pass";
    case ReceiverRequirementStatus::Fail:
        return "Fail";
    }
    return "Incomplete";
}

void setNumber(std::array<char, 128> &buffer, double value) {
    std::snprintf(buffer.data(), buffer.size(), "%.*g", std::numeric_limits<double>::max_digits10,
                  value);
}
} // namespace
ReceiverRequirementStatusTone receiverRequirementStatusTone(ReceiverRequirementStatus status) {
    switch (status) {
    case ReceiverRequirementStatus::Pass:
        return ReceiverRequirementStatusTone::PassGreen;
    case ReceiverRequirementStatus::Fail:
        return ReceiverRequirementStatusTone::FailRed;
    case ReceiverRequirementStatus::NotConfigured:
    case ReceiverRequirementStatus::InvalidConfiguration:
    case ReceiverRequirementStatus::Incomplete:
        return ReceiverRequirementStatusTone::Neutral;
    }
    return ReceiverRequirementStatusTone::Neutral;
}

namespace {
void drawStatusRow(const char *label, ReceiverRequirementStatus status) {
    switch (receiverRequirementStatusTone(status)) {
    case ReceiverRequirementStatusTone::PassGreen:
        ImGui::TextColored(ImVec4(0.25f, 0.85f, 0.35f, 1.0f), "%s: %s", label, statusText(status));
        break;
    case ReceiverRequirementStatusTone::FailRed:
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s: %s", label, statusText(status));
        break;
    case ReceiverRequirementStatusTone::Neutral:
        ImGui::TextDisabled("%s: %s", label, statusText(status));
        break;
    }
}
} // namespace

bool ReceiverRequirementsWidget::stateMatchesSnapshot(
    const ReceiverRequirementsState &state) const {
    if (state.invalid_reason != m_snapshot.invalid_reason ||
        state.config.has_value() != m_snapshot.config.has_value())
        return false;
    if (!state.config)
        return true;
    const auto &a = *state.config;
    const auto &b = *m_snapshot.config;
    return a.band_start_Hz == b.band_start_Hz && a.band_stop_Hz == b.band_stop_Hz &&
           a.gain_min_dB == b.gain_min_dB && a.gain_max_dB == b.gain_max_dB &&
           a.nf_max_dB == b.nf_max_dB;
}

void ReceiverRequirementsWidget::resetDraft(const ReceiverRequirementsState &state) {
    for (auto &buffer : m_buffers)
        buffer.fill('\0');
    if (state.config && state.invalid_reason.empty()) {
        const auto &config = *state.config;
        setNumber(m_buffers[0], config.band_start_Hz);
        setNumber(m_buffers[1], config.band_stop_Hz);
        setNumber(m_buffers[2], config.gain_min_dB);
        setNumber(m_buffers[3], config.gain_max_dB);
        setNumber(m_buffers[4], config.nf_max_dB);
    }
    for (std::size_t i = 0; i < m_baseline.size(); ++i)
        m_baseline[i] = m_buffers[i].data();
    if (!state.config || !state.invalid_reason.empty())
        m_baseline = {};
    m_snapshot = state;
    m_initialized = true;
    m_error.clear();
}

void ReceiverRequirementsWidget::draw(const char *title, bool *p_open,
                                      ReceiverRequirementsState &state,
                                      const ReceiverRequirementsEvaluation &result) {
    if (!m_initialized || !stateMatchesSnapshot(state))
        resetDraft(state);

    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    for (std::size_t i = 0; i < m_buffers.size(); ++i)
        ImGui::InputText(kLabels[i], m_buffers[i].data(), m_buffers[i].size());

    const auto makeDraft = [this]() {
        ReceiverRequirementsDraft draft;
        for (std::size_t i = 0; i < draft.size(); ++i)
            draft[i] = m_buffers[i].data();
        return draft;
    };

    if (ImGui::Button("Apply requirements")) {
        if (applyReceiverRequirementsDraft(state, makeDraft(), m_error)) {
            resetDraft(state);
            if (onChange)
                onChange();
            if (p_open)
                *p_open = false;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        resetDraft(state);
    const bool unapplied = makeDraft() != m_baseline;

    if (!m_error.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_error.c_str());
    else if (!state.invalid_reason.empty())
        ImGui::TextDisabled("Invalid configuration: %s", state.invalid_reason.c_str());
    else if (!state.config)
        ImGui::TextDisabled("Not configured");

    if (unapplied)
        ImGui::TextDisabled("Unapplied edits");
    else if (state.invalid_reason.empty() && state.config) {
        ImGui::Separator();
        drawStatusRow("Gain", result.gain.status);
        if (result.gain.observed_min_dB && result.gain.observed_max_dB)
            ImGui::Text("Observed gain: %.3f to %.3f dB", *result.gain.observed_min_dB,
                        *result.gain.observed_max_dB);
        drawStatusRow("Noise figure", result.noise_figure.status);
        if (result.noise_figure.observed_max_dB)
            ImGui::Text("Observed NF max: %.3f dB", *result.noise_figure.observed_max_dB);
        drawStatusRow("Overall", result.overall);
    }
    ImGui::End();
}
