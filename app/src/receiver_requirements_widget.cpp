#include "receiver_requirements_widget.h"
#include "imgui.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace {
constexpr const char *kLabels[] = {
    "Band start (Hz)",        "Band stop (Hz)",        "Gain min (dB)",
    "Gain max (dB)",          "NF max (dB)",           "Output power min (dBm)",
    "Output power max (dBm)", "IIP3 min (dBm)",        "IIP3 tone spacing (Hz)",
    "IIP3 input start (dBm)", "IIP3 input stop (dBm)", "IIP3 input step (dB)"};

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

bool sameDraft(const ReceiverRequirementsDraft &a, const ReceiverRequirementsDraft &b) {
    return a.band_start_Hz == b.band_start_Hz && a.band_stop_Hz == b.band_stop_Hz &&
           a.gain_min_dB == b.gain_min_dB && a.gain_max_dB == b.gain_max_dB &&
           a.nf_max_dB == b.nf_max_dB && a.output_power_min_dBm == b.output_power_min_dBm &&
           a.output_power_max_dBm == b.output_power_max_dBm && a.iip3_min_dBm == b.iip3_min_dBm &&
           a.iip3_tone_spacing_Hz == b.iip3_tone_spacing_Hz &&
           a.iip3_input_start_dBm == b.iip3_input_start_dBm &&
           a.iip3_input_stop_dBm == b.iip3_input_stop_dBm &&
           a.iip3_input_step_dB == b.iip3_input_step_dB && a.gain_enabled == b.gain_enabled &&
           a.noise_figure_enabled == b.noise_figure_enabled &&
           a.output_power_enabled == b.output_power_enabled && a.iip3_enabled == b.iip3_enabled &&
           a.output_reference_tone_frequency_Hz == b.output_reference_tone_frequency_Hz;
}

std::string toneLabel(double frequency_Hz) {
    char label[64]{};
    std::snprintf(label, sizeof(label), "%g MHz", frequency_Hz / 1.0e6);
    return label;
}

void drawStatusRow(const char *label, const ReceiverMetricEvaluation &metric, const char *unit) {
    switch (receiverRequirementStatusTone(metric.status)) {
    case ReceiverRequirementStatusTone::PassGreen:
        ImGui::TextColored(ImVec4(0.25f, 0.85f, 0.35f, 1.0f), "%s: %s", label,
                           statusText(metric.status));
        break;
    case ReceiverRequirementStatusTone::FailRed:
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s: %s", label,
                           statusText(metric.status));
        break;
    case ReceiverRequirementStatusTone::Neutral:
        ImGui::TextDisabled("%s: %s", label, statusText(metric.status));
        break;
    }
    if (metric.observed_min && metric.observed_max) {
        if (*metric.observed_min == *metric.observed_max)
            ImGui::Text("Observed %s: %.3f %s", label, *metric.observed_min, unit);
        else
            ImGui::Text("Observed %s: %.3f to %.3f %s", label, *metric.observed_min,
                        *metric.observed_max, unit);
    }
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
           a.gain.has_value() == b.gain.has_value() &&
           (!a.gain || (a.gain->minimum_dB == b.gain->minimum_dB &&
                        a.gain->maximum_dB == b.gain->maximum_dB)) &&
           a.nf_max_dB == b.nf_max_dB && a.output_power.has_value() == b.output_power.has_value() &&
           (!a.output_power || (a.output_power->minimum_dBm == b.output_power->minimum_dBm &&
                                a.output_power->maximum_dBm == b.output_power->maximum_dBm)) &&
           a.iip3_min_dBm == b.iip3_min_dBm &&
           a.measurement_conditions.output_reference_tone_frequency_Hz ==
               b.measurement_conditions.output_reference_tone_frequency_Hz &&
           a.measurement_conditions.iip3.has_value() == b.measurement_conditions.iip3.has_value() &&
           (!a.measurement_conditions.iip3 || (a.measurement_conditions.iip3->tone_spacing_Hz ==
                                                   b.measurement_conditions.iip3->tone_spacing_Hz &&
                                               a.measurement_conditions.iip3->input_start_dBm ==
                                                   b.measurement_conditions.iip3->input_start_dBm &&
                                               a.measurement_conditions.iip3->input_stop_dBm ==
                                                   b.measurement_conditions.iip3->input_stop_dBm &&
                                               a.measurement_conditions.iip3->input_step_dB ==
                                                   b.measurement_conditions.iip3->input_step_dB));
}

ReceiverRequirementsDraft ReceiverRequirementsWidget::makeDraft() const {
    ReceiverRequirementsDraft draft;
    draft.band_start_Hz = m_buffers[0].data();
    draft.band_stop_Hz = m_buffers[1].data();
    draft.gain_min_dB = m_buffers[2].data();
    draft.gain_max_dB = m_buffers[3].data();
    draft.nf_max_dB = m_buffers[4].data();
    draft.output_power_min_dBm = m_buffers[5].data();
    draft.output_power_max_dBm = m_buffers[6].data();
    draft.iip3_min_dBm = m_buffers[7].data();
    draft.iip3_tone_spacing_Hz = m_buffers[8].data();
    draft.iip3_input_start_dBm = m_buffers[9].data();
    draft.iip3_input_stop_dBm = m_buffers[10].data();
    draft.iip3_input_step_dB = m_buffers[11].data();
    draft.gain_enabled = m_gain_enabled;
    draft.noise_figure_enabled = m_nf_enabled;
    draft.output_power_enabled = m_output_power_enabled;
    draft.iip3_enabled = m_iip3_enabled;
    draft.output_reference_tone_frequency_Hz = m_source_tone_frequency_Hz;
    return draft;
}

void ReceiverRequirementsWidget::resetDraft(
    const ReceiverRequirementsState &state,
    const std::vector<ReceiverGeneratorToneOption> &source_tones) {
    for (auto &buffer : m_buffers)
        buffer.fill('\0');
    m_gain_enabled = m_nf_enabled = m_output_power_enabled = m_iip3_enabled = false;
    m_source_tone_frequency_Hz.reset();
    if (state.config && state.invalid_reason.empty()) {
        const auto &config = *state.config;
        setNumber(m_buffers[0], config.band_start_Hz);
        setNumber(m_buffers[1], config.band_stop_Hz);
        if (config.gain) {
            m_gain_enabled = true;
            setNumber(m_buffers[2], config.gain->minimum_dB);
            setNumber(m_buffers[3], config.gain->maximum_dB);
        }
        if (config.nf_max_dB) {
            m_nf_enabled = true;
            setNumber(m_buffers[4], *config.nf_max_dB);
        }
        if (config.output_power) {
            m_output_power_enabled = true;
            setNumber(m_buffers[5], config.output_power->minimum_dBm);
            setNumber(m_buffers[6], config.output_power->maximum_dBm);
        }
        if (config.iip3_min_dBm) {
            m_iip3_enabled = true;
            setNumber(m_buffers[7], *config.iip3_min_dBm);
        }
        if (config.measurement_conditions.iip3) {
            const auto &settings = *config.measurement_conditions.iip3;
            setNumber(m_buffers[8], settings.tone_spacing_Hz);
            setNumber(m_buffers[9], settings.input_start_dBm);
            setNumber(m_buffers[10], settings.input_stop_dBm);
            setNumber(m_buffers[11], settings.input_step_dB);
        }
        m_source_tone_frequency_Hz =
            config.measurement_conditions.output_reference_tone_frequency_Hz;
    }
    if (!m_source_tone_frequency_Hz && source_tones.size() == 1)
        m_source_tone_frequency_Hz = source_tones.front().frequency_Hz;
    m_baseline = makeDraft();
    m_snapshot = state;
    m_initialized = true;
    m_error.clear();
}

void ReceiverRequirementsWidget::draw(
    const char *title, bool *p_open, ReceiverRequirementsState &state,
    const ReceiverRequirementsEvaluation &result,
    const std::vector<ReceiverGeneratorToneOption> &source_tones) {
    if (!m_initialized || !stateMatchesSnapshot(state))
        resetDraft(state, source_tones);

    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    ImGui::InputText(kLabels[0], m_buffers[0].data(), m_buffers[0].size());
    ImGui::InputText(kLabels[1], m_buffers[1].data(), m_buffers[1].size());
    ImGui::Checkbox("Enable gain", &m_gain_enabled);
    if (m_gain_enabled) {
        ImGui::InputText(kLabels[2], m_buffers[2].data(), m_buffers[2].size());
        ImGui::InputText(kLabels[3], m_buffers[3].data(), m_buffers[3].size());
    }
    ImGui::Checkbox("Enable noise figure", &m_nf_enabled);
    if (m_nf_enabled)
        ImGui::InputText(kLabels[4], m_buffers[4].data(), m_buffers[4].size());
    if (!m_source_tone_frequency_Hz && source_tones.size() == 1)
        m_source_tone_frequency_Hz = source_tones.front().frequency_Hz;
    ImGui::Checkbox("Enable output power", &m_output_power_enabled);
    if (m_output_power_enabled) {
        ImGui::InputText(kLabels[5], m_buffers[5].data(), m_buffers[5].size());
        ImGui::InputText(kLabels[6], m_buffers[6].data(), m_buffers[6].size());
        if (source_tones.empty()) {
            ImGui::TextDisabled("Output reference tone: unavailable");
        } else {
            std::string preview = "Select tone";
            if (m_source_tone_frequency_Hz) {
                std::size_t matches = 0;
                for (const auto &tone : source_tones)
                    matches += tone.frequency_Hz == *m_source_tone_frequency_Hz ? 1 : 0;
                preview = matches == 1 ? toneLabel(*m_source_tone_frequency_Hz)
                                       : toneLabel(*m_source_tone_frequency_Hz) + " (unavailable)";
            }
            if (ImGui::BeginCombo("Output reference tone", preview.c_str())) {
                for (std::size_t i = 0; i < source_tones.size(); ++i) {
                    const auto &tone = source_tones[i];
                    const std::string label = toneLabel(tone.frequency_Hz);
                    ImGui::PushID(static_cast<int>(i));
                    const bool selected = m_source_tone_frequency_Hz == tone.frequency_Hz;
                    if (ImGui::Selectable(label.c_str(), selected))
                        m_source_tone_frequency_Hz = tone.frequency_Hz;
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
        }
    }
    ImGui::Checkbox("Enable IIP3", &m_iip3_enabled);
    if (m_iip3_enabled) {
        ImGui::InputText(kLabels[7], m_buffers[7].data(), m_buffers[7].size());
        ImGui::InputText(kLabels[8], m_buffers[8].data(), m_buffers[8].size());
        ImGui::InputText(kLabels[9], m_buffers[9].data(), m_buffers[9].size());
        ImGui::InputText(kLabels[10], m_buffers[10].data(), m_buffers[10].size());
        ImGui::InputText(kLabels[11], m_buffers[11].data(), m_buffers[11].size());
    }

    const auto draft = makeDraft();
    if (ImGui::Button("Apply requirements")) {
        if (applyReceiverRequirementsDraft(state, draft, m_error)) {
            const bool changed = !stateMatchesSnapshot(state);
            resetDraft(state, source_tones);
            if (changed && onChange)
                onChange();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        resetDraft(state, source_tones);
    const bool unapplied = !sameDraft(makeDraft(), m_baseline);

    if (!m_error.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_error.c_str());
    else if (!state.invalid_reason.empty())
        ImGui::TextDisabled("Invalid configuration: %s", state.invalid_reason.c_str());
    else if (!state.config)
        ImGui::TextDisabled("Not configured");

    if (unapplied) {
        ImGui::TextDisabled("Unapplied edits");
    } else if (state.invalid_reason.empty() && state.config) {
        ImGui::Separator();
        drawStatusRow("Gain", result.gain, "dB");
        drawStatusRow("Noise figure", result.noise_figure, "dB");
        drawStatusRow("Output power", result.output_power, "dBm");
        drawStatusRow("IIP3", result.iip3, "dBm");
        switch (receiverRequirementStatusTone(result.overall)) {
        case ReceiverRequirementStatusTone::PassGreen:
            ImGui::TextColored(ImVec4(0.25f, 0.85f, 0.35f, 1.0f), "Overall: %s",
                               statusText(result.overall));
            break;
        case ReceiverRequirementStatusTone::FailRed:
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Overall: %s",
                               statusText(result.overall));
            break;
        case ReceiverRequirementStatusTone::Neutral:
            ImGui::TextDisabled("Overall: %s", statusText(result.overall));
            break;
        }
    }
    ImGui::End();
}
