#include "receiver_requirements_widget.h"
#include "imgui.h"
#include <cstdio>
#include <limits>
#include <string>

namespace {
constexpr const char *kLabels[] = {
    "Band start (Hz)",        "Band stop (Hz)",        "Gain min (dB)",
    "Gain max (dB)",          "NF max (dB)",           "Output power min (dBm)",
    "Output power max (dBm)", "IIP3 min (dBm)",        "IIP3 tone spacing (Hz)",
    "IIP3 input start (dBm)", "IIP3 input stop (dBm)", "IIP3 input step (dB)",
    "Input level (dBm)"};

constexpr ImVec4 kErrorColor(1.0f, 0.35f, 0.35f, 1.0f);
constexpr ImVec4 kPassColor(0.25f, 0.85f, 0.35f, 1.0f);

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
           a.input_level_dBm == b.input_level_dBm;
}

void drawStatusRow(const char *label, const ReceiverMetricEvaluation &metric, const char *unit) {
    switch (receiverRequirementStatusTone(metric.status)) {
    case ReceiverRequirementStatusTone::PassGreen:
        ImGui::TextColored(kPassColor, "%s: %s", label, statusText(metric.status));
        break;
    case ReceiverRequirementStatusTone::FailRed:
        ImGui::TextColored(kErrorColor, "%s: %s", label, statusText(metric.status));
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
    draft.input_level_dBm = m_buffers[12].data();
    return draft;
}

void ReceiverRequirementsWidget::fillModeName(const ReceiverRequirementsSet &set) {
    m_mode_name.fill('\0');
    m_name_selected = set.selected;
    if (set.selected < set.modes.size()) {
        const std::string &name = set.modes[set.selected].name;
        std::snprintf(m_mode_name.data(), m_mode_name.size(), "%s", name.c_str());
    }
}

void ReceiverRequirementsWidget::resetDraft(const ReceiverRequirementsSet &set) {
    for (auto &buffer : m_buffers)
        buffer.fill('\0');
    m_gain_enabled = m_nf_enabled = m_output_power_enabled = m_iip3_enabled = false;
    const auto state = selectedReceiverRequirementsState(set);
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
        if (config.measurement_conditions.input_level_dBm)
            setNumber(m_buffers[12], *config.measurement_conditions.input_level_dBm);
    }
    m_baseline = makeDraft();
    m_snapshot = set;
    fillModeName(set);
    m_initialized = true;
    m_error.clear();
}

void ReceiverRequirementsWidget::draw(const char *title, bool *p_open, ReceiverRequirementsSet &set,
                                      const ReceiverRequirementsEvaluation &result) {
    // The snapshot covers the whole set, so an external edit (for example an
    // agent change or a different selection) reloads the draft from the
    // selected mode instead of overwriting the new state.
    if (!m_initialized || !(set == m_snapshot))
        resetDraft(set);

    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    const bool has_modes = !set.modes.empty();
    // validateReceiverRequirementsSet() keeps selected in range; clamp anyway so
    // a test seam writing the set directly cannot index past the modes.
    const std::size_t selected_index =
        has_modes && set.selected < set.modes.size() ? set.selected : 0;
    if (has_modes && set.selected != m_name_selected)
        fillModeName(set);
    const bool unapplied = !sameDraft(makeDraft(), m_baseline);

    ImGui::BeginDisabled(unapplied);
    const char *preview = has_modes ? set.modes[selected_index].name.c_str() : "No modes";
    if (ImGui::BeginCombo("Mode", preview)) {
        for (std::size_t index = 0; index < set.modes.size(); ++index) {
            ImGui::PushID(static_cast<int>(index));
            const bool selected = index == selected_index;
            if (ImGui::Selectable(set.modes[index].name.c_str(), selected)) {
                if (selectReceiverRequirementsMode(set, index)) {
                    resetDraft(set);
                    if (onChange)
                        onChange();
                }
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::InputText("Mode name", m_mode_name.data(), m_mode_name.size());
    if (ImGui::Button("Add mode")) {
        std::string error;
        if (addReceiverRequirementsMode(set, m_mode_name.data(), error)) {
            resetDraft(set);
            if (onChange)
                onChange();
        } else {
            m_error = std::move(error);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Rename mode")) {
        if (!has_modes) {
            m_error = "Add a mode before renaming.";
        } else {
            std::string error;
            if (renameReceiverRequirementsMode(set, set.selected, m_mode_name.data(), error)) {
                resetDraft(set);
                if (onChange)
                    onChange();
            } else {
                m_error = std::move(error);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove mode")) {
        if (removeReceiverRequirementsMode(set, set.selected)) {
            resetDraft(set);
            if (onChange)
                onChange();
        }
    }
    ImGui::EndDisabled();
    // Both lines always occupy their space, so an appearing hint or error never
    // shifts the limit fields under the mouse or a scripted click.
    if (unapplied)
        ImGui::TextDisabled("Apply or cancel edits to change modes");
    else
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
    if (!m_error.empty())
        ImGui::TextColored(kErrorColor, "%s", m_error.c_str());
    else
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));

    if (!has_modes) {
        ImGui::TextDisabled("No modes. Add a mode to define requirements.");
    } else {
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
        ImGui::Checkbox("Enable output power", &m_output_power_enabled);
        if (m_output_power_enabled) {
            ImGui::InputText(kLabels[5], m_buffers[5].data(), m_buffers[5].size());
            ImGui::InputText(kLabels[6], m_buffers[6].data(), m_buffers[6].size());
        }
        ImGui::InputText(kLabels[12], m_buffers[12].data(), m_buffers[12].size());
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
            std::string error;
            if (applyReceiverRequirementsDraftToSelectedMode(set, draft, error)) {
                const bool changed = !(set == m_snapshot);
                resetDraft(set);
                if (changed && onChange)
                    onChange();
            } else {
                m_error = std::move(error);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            resetDraft(set);
    }

    const bool pending_edits = !sameDraft(makeDraft(), m_baseline);
    if (pending_edits) {
        ImGui::TextDisabled("Unapplied edits");
    } else {
        const auto state = selectedReceiverRequirementsState(set);
        if (!state.invalid_reason.empty()) {
            ImGui::TextDisabled("Invalid configuration: %s", state.invalid_reason.c_str());
        } else if (!state.config) {
            ImGui::TextDisabled("Not configured");
        } else {
            ImGui::Separator();
            drawStatusRow("Gain", result.gain, "dB");
            drawStatusRow("Noise figure", result.noise_figure, "dB");
            drawStatusRow("Output power", result.output_power, "dBm");
            if (state.config->output_power && !state.config->measurement_conditions.input_level_dBm)
                ImGui::TextDisabled("Needs input level");
            drawStatusRow("IIP3", result.iip3, "dBm");
            switch (receiverRequirementStatusTone(result.overall)) {
            case ReceiverRequirementStatusTone::PassGreen:
                ImGui::TextColored(kPassColor, "Overall: %s", statusText(result.overall));
                break;
            case ReceiverRequirementStatusTone::FailRed:
                ImGui::TextColored(kErrorColor, "Overall: %s", statusText(result.overall));
                break;
            case ReceiverRequirementStatusTone::Neutral:
                ImGui::TextDisabled("Overall: %s", statusText(result.overall));
                break;
            }
        }
    }
    ImGui::End();
}
