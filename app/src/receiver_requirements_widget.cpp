#include "receiver_requirements_widget.h"

#include "imgui.h"
#include "rf_view_ui.h"

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

constexpr const char *kRemoveModeTitle = "Remove mode?";
constexpr float kFieldWidth = 200.0f;
constexpr float kNameWidth = 260.0f;

const char *const kNoModesHint =
    "Type a name in Mode name, then choose Add mode to define the first mode.";

void setNumber(std::array<char, 128> &buffer, double value) {
    std::snprintf(buffer.data(), buffer.size(), "%.*g", std::numeric_limits<double>::max_digits10,
                  value);
}

bool sameDraft(const ReceiverRequirementsDraft &a, const ReceiverRequirementsDraft &b) {
    if (a.oob_suppression.size() != b.oob_suppression.size())
        return false;
    for (std::size_t i = 0; i < a.oob_suppression.size(); ++i) {
        if (a.oob_suppression[i].anchor != b.oob_suppression[i].anchor ||
            a.oob_suppression[i].offset_Hz != b.oob_suppression[i].offset_Hz ||
            a.oob_suppression[i].min_dB != b.oob_suppression[i].min_dB)
            return false;
    }
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

// True when both sets select the same mode with the same name and config, so an unapplied draft
// of that mode still describes it. A retained invalid set matches only the same diagnostic.
bool sameSelectedMode(const ReceiverRequirementsSet &a, const ReceiverRequirementsSet &b) {
    if (a.invalid_reason != b.invalid_reason)
        return false;
    const bool has_mode = a.selected < a.modes.size();
    if (has_mode != (b.selected < b.modes.size()))
        return false;
    if (!has_mode)
        return true;
    const ReceiverRequirementsMode &mode_a = a.modes[a.selected];
    const ReceiverRequirementsMode &mode_b = b.modes[b.selected];
    return mode_a.name == mode_b.name && mode_a.config == mode_b.config;
}

// A limit rendered for the result table: "0 to 20 dB", "max 5 dB", "min 10 dBm", or "-" when the
// metric is not required.
std::string limitRange(const std::optional<double> &low, const std::optional<double> &high,
                       const char *unit) {
    char text[96];
    if (low && high)
        std::snprintf(text, sizeof(text), "%.3g to %.3g %s", *low, *high, unit);
    else if (high)
        std::snprintf(text, sizeof(text), "max %.3g %s", *high, unit);
    else if (low)
        std::snprintf(text, sizeof(text), "min %.3g %s", *low, unit);
    else
        std::snprintf(text, sizeof(text), "-");
    return text;
}

void drawMetricRow(const char *label, const std::string &limit,
                   const ReceiverMetricEvaluation &metric, const char *unit) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    if (limit == "-")
        ImGui::TextDisabled("-");
    else
        ImGui::TextUnformatted(limit.c_str());
    ImGui::TableNextColumn();
    rf_view_ui::observedText(metric, unit);
    ImGui::TableNextColumn();
    rf_view_ui::metricStatus(metric);
}

void drawEvaluation(const ReceiverRequirementsState &state,
                    const ReceiverRequirementsEvaluation &result) {
    if (!state.invalid_reason.empty()) {
        rf_view_ui::hint(state.invalid_reason.c_str());
        return;
    }
    if (!state.config) {
        rf_view_ui::hint("Not configured. Fill the limits above and choose Apply requirements.");
        return;
    }
    const ReceiverRequirementsConfig &config = *state.config;

    rf_view_ui::sectionHeader("Measured result");
    if (ImGui::BeginTable("requirements_evaluation", 4,
                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Limit", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Observed", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableHeadersRow();
        drawMetricRow(
            "Gain",
            limitRange(config.gain ? std::optional<double>(config.gain->minimum_dB) : std::nullopt,
                       config.gain ? std::optional<double>(config.gain->maximum_dB) : std::nullopt,
                       "dB"),
            result.gain, "dB");
        drawMetricRow("Noise figure", limitRange(std::nullopt, config.nf_max_dB, "dB"),
                      result.noise_figure, "dB");
        drawMetricRow(
            "Output power",
            limitRange(config.output_power ? std::optional<double>(config.output_power->minimum_dBm)
                                           : std::nullopt,
                       config.output_power ? std::optional<double>(config.output_power->maximum_dBm)
                                           : std::nullopt,
                       "dBm"),
            result.output_power, "dBm");
        drawMetricRow("IIP3", limitRange(config.iip3_min_dBm, std::nullopt, "dBm"), result.iip3,
                      "dBm");
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Overall");
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();
        rf_view_ui::statusBadge(result.overall);
        ImGui::EndTable();
    }
    if (config.output_power && !config.measurement_conditions.input_level_dBm)
        rf_view_ui::hint("Output power is measured only with an input level set above.");
}

void drawBudgetPrediction(const RfBudgetEvaluation &evaluation, const RfBudgetModeResult *budget) {
    if (!evaluation.not_evaluated_reason.empty() ||
        (budget && budget->status == RfBudgetModeStatus::NotConfigured)) {
        rf_view_ui::sectionHeader("Budget prediction");
        // An invalid or unevaluated budget returns no mode results at all. Saying "not budgeted"
        // there would hide the real reason.
        if (!evaluation.not_evaluated_reason.empty()) {
            rf_view_ui::hint(evaluation.not_evaluated_reason.c_str());
            return;
        }
        rf_view_ui::hint("The mode has no requirements.");
        return;
    }
    rf_view_ui::sectionHeader("Budget prediction");
    if (!budget) {
        rf_view_ui::hint("No budget record for this mode. View > RF Budget adds one.");
        return;
    }
    if (budget->status == RfBudgetModeStatus::NotBudgeted) {
        rf_view_ui::hint("Not budgeted for this mode. Use View > RF Budget's Budget this mode "
                         "button.");
        return;
    }
    if (budget->cascades.empty())
        ImGui::TextDisabled("No output is reached by this mode.");
    if (!budget->cascades.empty() &&
        ImGui::BeginTable("budget_prediction", 7,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("Output");
        ImGui::TableSetupColumn("Overall");
        ImGui::TableSetupColumn("Gain");
        ImGui::TableSetupColumn("NF");
        ImGui::TableSetupColumn("Output power");
        ImGui::TableSetupColumn("IIP3");
        ImGui::TableSetupColumn("OOB");
        ImGui::TableHeadersRow();
        for (const RfBudgetCascade &cascade : budget->cascades) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(cascade.output.c_str());
            ImGui::TableNextColumn();
            rf_view_ui::metricStatus(
                ReceiverMetricEvaluation{cascade.verdict.overall, std::nullopt, std::nullopt});
            ImGui::TableNextColumn();
            rf_view_ui::metricStatus(cascade.verdict.gain);
            ImGui::TableNextColumn();
            rf_view_ui::metricStatus(cascade.verdict.noise_figure);
            ImGui::TableNextColumn();
            rf_view_ui::metricStatus(cascade.verdict.output_power);
            ImGui::TableNextColumn();
            rf_view_ui::metricStatus(cascade.verdict.iip3);
            ImGui::TableNextColumn();
            rf_view_ui::metricStatus(cascade.verdict.oob_suppression);
        }
        ImGui::EndTable();
    }
    for (const RfBudgetUnreachedOutput &unreached : budget->unreached)
        ImGui::TextColored(rf_view_ui::warnColor(), "%s: %s", unreached.output.c_str(),
                           unreached.reason.c_str());
}

// One numeric limit field, indented under its section heading.
void drawNumberField(const char *label, std::array<char, 128> &buffer, float width) {
    ImGui::SetNextItemWidth(width);
    ImGui::InputText(label, buffer.data(), buffer.size());
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
    draft.oob_suppression.reserve(m_oob_rows.size());
    for (const auto &row : m_oob_rows)
        draft.oob_suppression.push_back({row.anchor, row.offset.data(), row.min.data()});
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
    m_oob_rows.clear();
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
        for (const auto &point : config.oob_suppression) {
            OobRow row;
            row.anchor = point.anchor == ReceiverOOBSuppressionAnchor::BandStart ? 0 : 1;
            setNumber(row.offset, point.offset_Hz);
            setNumber(row.min, point.min_dB);
            m_oob_rows.push_back(row);
        }
    }
    m_baseline = makeDraft();
    m_snapshot = set;
    fillModeName(set);
    m_initialized = true;
    m_error.clear();
    m_mode_error.clear();
    m_notice.clear();
}

void ReceiverRequirementsWidget::draw(const char *title, bool *p_open, ReceiverRequirementsSet &set,
                                      const ReceiverRequirementsEvaluation &result,
                                      const RfBudgetEvaluation &budget) {
    // An external change reloads the draft only when it changes the selected mode.
    // Edits to other modes keep the unapplied draft, which still describes the selected mode.
    if (!m_initialized) {
        resetDraft(set);
    } else if (!(set == m_snapshot)) {
        if (sameSelectedMode(set, m_snapshot)) {
            m_snapshot = set;
        } else {
            const bool discarded_edits = !sameDraft(makeDraft(), m_baseline);
            resetDraft(set);
            if (discarded_edits)
                m_notice = "Selected mode changed elsewhere; unapplied edits were discarded.";
        }
    }

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

    // --- Mode selection and naming -------------------------------------------------------------
    rf_view_ui::sectionHeader("Mode");
    ImGui::BeginDisabled(unapplied);
    const char *preview = has_modes ? set.modes[selected_index].name.c_str() : "No modes";
    ImGui::SetNextItemWidth(kNameWidth);
    if (ImGui::BeginCombo("Mode", preview)) {
        for (std::size_t index = 0; index < set.modes.size(); ++index) {
            ImGui::PushID(static_cast<int>(index));
            const bool selected = index == selected_index;
            // Choosing a mode changes the view, not the project, so nothing is marked dirty.
            if (ImGui::Selectable(set.modes[index].name.c_str(), selected) && !selected &&
                selectReceiverRequirementsMode(set, index))
                resetDraft(set);
            if (selected)
                ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(kNameWidth);
    if (ImGui::InputText("Mode name", m_mode_name.data(), m_mode_name.size()))
        m_mode_error.clear();
    ImGui::SameLine();
    if (ImGui::Button("Add mode")) {
        std::string error;
        if (addReceiverRequirementsMode(set, m_mode_name.data(), error)) {
            resetDraft(set);
            if (onChange)
                onChange();
        } else {
            m_mode_error = std::move(error);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Rename mode")) {
        if (!has_modes) {
            m_mode_error = "Add a mode before renaming.";
        } else {
            std::string error;
            if (renameReceiverRequirementsMode(set, set.selected, m_mode_name.data(), error)) {
                resetDraft(set);
                if (onChange)
                    onChange();
            } else {
                m_mode_error = std::move(error);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove mode")) {
        if (has_modes) {
            m_remove_id = set.modes[selected_index].id;
            ImGui::OpenPopup(kRemoveModeTitle);
        } else {
            m_mode_error = "Add a mode before removing.";
        }
    }
    ImGui::EndDisabled();
    // Removing a mode deletes its limits, so it waits for confirmation. The mode is matched by its
    // id when confirmed, so a set changed meanwhile never removes a different mode.
    if (ImGui::BeginPopupModal(kRemoveModeTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const ReceiverRequirementsMode *pending = findReceiverRequirementsMode(set, m_remove_id);
        ImGui::Text("Remove mode \"%s\" and its requirement limits?",
                    pending ? pending->name.c_str() : "");
        if (ImGui::Button("Remove", ImVec2(120.0f, 0.0f))) {
            for (std::size_t index = 0; index < set.modes.size(); ++index) {
                if (set.modes[index].id == m_remove_id &&
                    removeReceiverRequirementsMode(set, index)) {
                    resetDraft(set);
                    if (onChange)
                        onChange();
                    break;
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep mode", ImVec2(120.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    // Both lines always occupy their space, so an appearing hint or error never
    // shifts the limit fields under the mouse or a scripted click.
    if (!m_mode_error.empty())
        ImGui::TextColored(rf_view_ui::failColor(), "%s", m_mode_error.c_str());
    else if (unapplied)
        ImGui::TextDisabled("Apply or cancel edits to change modes");
    else
        ImGui::TextDisabled(has_modes ? "Add mode uses the name in Mode name." : kNoModesHint);
    if (!m_notice.empty())
        ImGui::TextDisabled("%s", m_notice.c_str());
    else
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));

    if (!has_modes) {
        ImGui::End();
        return;
    }

    // --- Limits ---------------------------------------------------------------------------------
    rf_view_ui::sectionHeader("Band");
    ImGui::PushItemWidth(kFieldWidth);
    ImGui::InputText(kLabels[0], m_buffers[0].data(), m_buffers[0].size());
    ImGui::InputText(kLabels[1], m_buffers[1].data(), m_buffers[1].size());
    ImGui::PopItemWidth();

    rf_view_ui::sectionHeader("Gain");
    ImGui::Checkbox("Enable gain", &m_gain_enabled);
    ImGui::BeginDisabled(!m_gain_enabled);
    ImGui::Indent(ImGui::GetStyle().IndentSpacing * 0.5f);
    drawNumberField(kLabels[2], m_buffers[2], kFieldWidth);
    drawNumberField(kLabels[3], m_buffers[3], kFieldWidth);
    ImGui::Unindent(ImGui::GetStyle().IndentSpacing * 0.5f);
    ImGui::EndDisabled();

    rf_view_ui::sectionHeader("Out-of-band suppression");
    ImGui::TextDisabled("Each point needs gain limits: the cascade gain at the point must be");
    ImGui::TextDisabled("at least its minimum below the gain minimum.");
    for (std::size_t row = 0; row < m_oob_rows.size(); ++row) {
        ImGui::PushID(static_cast<int>(row));
        const char *anchors[] = {"Band start", "Band stop"};
        ImGui::SetNextItemWidth(130.0f);
        ImGui::Combo("Anchor", &m_oob_rows[row].anchor, anchors, 2);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(kFieldWidth);
        ImGui::InputText("Offset (Hz)", m_oob_rows[row].offset.data(),
                         m_oob_rows[row].offset.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputText("Min (dB)", m_oob_rows[row].min.data(), m_oob_rows[row].min.size());
        ImGui::SameLine();
        if (ImGui::Button("Remove point")) {
            m_oob_rows.erase(m_oob_rows.begin() + static_cast<std::ptrdiff_t>(row));
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    ImGui::BeginDisabled(m_oob_rows.size() >= kMaxReceiverOOBSuppressionPoints);
    if (ImGui::Button("Add out-of-band point"))
        m_oob_rows.push_back(OobRow{});
    ImGui::EndDisabled();

    rf_view_ui::sectionHeader("Noise figure");
    ImGui::Checkbox("Enable noise figure", &m_nf_enabled);
    ImGui::BeginDisabled(!m_nf_enabled);
    ImGui::Indent(ImGui::GetStyle().IndentSpacing * 0.5f);
    drawNumberField(kLabels[4], m_buffers[4], kFieldWidth);
    ImGui::Unindent(ImGui::GetStyle().IndentSpacing * 0.5f);
    ImGui::EndDisabled();

    rf_view_ui::sectionHeader("Output power");
    ImGui::Checkbox("Enable output power", &m_output_power_enabled);
    ImGui::Indent(ImGui::GetStyle().IndentSpacing * 0.5f);
    ImGui::BeginDisabled(!m_output_power_enabled);
    drawNumberField(kLabels[5], m_buffers[5], kFieldWidth);
    drawNumberField(kLabels[6], m_buffers[6], kFieldWidth);
    ImGui::EndDisabled();
    // The input level is a measurement condition, but it only gates output power, so it stays
    // next to the limit it feeds and remains editable when the limit is off.
    drawNumberField(kLabels[12], m_buffers[12], kFieldWidth);
    if (m_output_power_enabled)
        ImGui::TextDisabled("Output power is measured at this input level.");
    ImGui::Unindent(ImGui::GetStyle().IndentSpacing * 0.5f);

    rf_view_ui::sectionHeader("IIP3");
    ImGui::Checkbox("Enable IIP3", &m_iip3_enabled);
    ImGui::BeginDisabled(!m_iip3_enabled);
    ImGui::Indent(ImGui::GetStyle().IndentSpacing * 0.5f);
    drawNumberField(kLabels[7], m_buffers[7], kFieldWidth);
    ImGui::Spacing();
    ImGui::TextDisabled("Two-tone test");
    drawNumberField(kLabels[8], m_buffers[8], kFieldWidth);
    drawNumberField(kLabels[9], m_buffers[9], kFieldWidth);
    drawNumberField(kLabels[10], m_buffers[10], kFieldWidth);
    drawNumberField(kLabels[11], m_buffers[11], kFieldWidth);
    ImGui::Unindent(ImGui::GetStyle().IndentSpacing * 0.5f);
    ImGui::EndDisabled();

    // --- Actions -------------------------------------------------------------------------------
    rf_view_ui::sectionHeader("Apply");
    const auto draft = makeDraft();
    const bool pending_edits = !sameDraft(draft, m_baseline);
    if (pending_edits) {
        // The theme's hover/active shades make the primary action stand out while an edit is
        // pending, without introducing a colour outside the design system.
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_Header));
    }
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
    if (pending_edits)
        ImGui::PopStyleColor(3);
    // Applying resets the draft, so the pending state is re-read: the result table appears in the
    // same frame the edit was accepted instead of one frame later.
    const bool still_pending = !sameDraft(makeDraft(), m_baseline);
    ImGui::SameLine();
    ImGui::BeginDisabled(!still_pending && m_error.empty());
    if (ImGui::Button("Cancel"))
        resetDraft(set);
    ImGui::EndDisabled();
    ImGui::SameLine();
    rf_view_ui::reservedLine(still_pending ? "Unapplied edits" : "");
    // The refusal sits under the row that produced it and wraps, so it never clips and never
    // moves a control that is under the pointer.
    if (!m_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, rf_view_ui::failColor());
        ImGui::TextWrapped("%s", m_error.c_str());
        ImGui::PopStyleColor();
    }

    if (!still_pending)
        drawEvaluation(selectedReceiverRequirementsState(set), result);

    const RfBudgetModeResult *budget_mode = nullptr;
    if (!still_pending && has_modes)
        budget_mode = findRfBudgetModeResult(budget, set.modes[selected_index].id);
    if (!still_pending)
        drawBudgetPrediction(budget, budget_mode);
    ImGui::End();
}
