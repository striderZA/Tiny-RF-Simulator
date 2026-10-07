#pragma once

#include "imgui.h"
#include "logging_core.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace utils {

// The value a bounded numeric field commits when the user edits it from
// `before` to `edited`, or nullopt when nothing should be written.
//
// The limits bound user input only. A value that reaches the engine some other
// way (a library definition, a loaded project, an API call) can lie outside
// them; the field shows that value and keeps it until the user edits it
// (issue #182). An edit is clamped into [lower, upper], but it is ignored when
// clamping would move the value against the edit: a `+` step on a value above
// `upper` would otherwise lower it to `upper`. An edit that leaves the value
// unchanged commits nothing, so it cannot dirty the project.
inline std::optional<double> boundedEdit(double before, double edited, double lower, double upper) {
    if (edited == before)
        return std::nullopt;
    const double clamped = std::clamp(edited, lower, upper);
    const bool against_edit =
        (edited > before && clamped < before) || (edited < before && clamped > before);
    if (against_edit || clamped == before)
        return std::nullopt;
    return clamped;
}

namespace detail {

// ImGui hides a label from "##" on; name the field the way the user sees it,
// or by its ID text when it has no visible label (e.g. "##freq").
inline std::string visibleLabel(std::string_view label) {
    const size_t hidden = label.find("##");
    if (hidden == std::string_view::npos)
        return std::string(label);
    if (hidden > 0)
        return std::string(label.substr(0, hidden));
    const size_t id = label.find_first_not_of('#');
    return std::string(id == std::string_view::npos ? label : label.substr(id));
}

// Shared body of inputDouble() and inputFrequency(). `value` and the limits are
// in base units; the field shows and steps `value / displayUnit`.
inline bool boundedInput(const char *label, double &value, double minorStep, double majorStep,
                         const char *format, double lower, double upper, double displayUnit) {
    const bool out_of_range = value < lower || value > upper;

    // Log an out-of-range value once, not on every frame the field is drawn.
    // The window's state storage keeps the last value logged under the
    // field's ID, rounded to float (the storage's widest number type) and
    // saturated rather than overflowed, so values equal at float precision
    // count as one. The ID names the field, not the component: selecting
    // another component holding the same value logs nothing new. Drawing an
    // in-range value clears the entry. The amber flag below is drawn on every
    // frame regardless.
    ImGui::PushID(label);
    const ImGuiID warned_key = ImGui::GetID("##out_of_range_warned");
    ImGui::PopID();
    ImGuiStorage *storage = ImGui::GetStateStorage();
    constexpr float kNotWarned = std::numeric_limits<float>::quiet_NaN();
    constexpr double kFloatMax = std::numeric_limits<float>::max();
    const float warned = storage->GetFloat(warned_key, kNotWarned);
    const float value_key = static_cast<float>(std::clamp(value, -kFloatMax, kFloatMax));
    if (out_of_range && value_key != warned) {
        LOG_WARN("%s: %g is outside the editable range %g to %g; the component keeps it "
                 "until the field is edited",
                 visibleLabel(label).c_str(), value / displayUnit, lower / displayUnit,
                 upper / displayUnit);
        storage->SetFloat(warned_key, value_key);
    } else if (!out_of_range && !std::isnan(warned)) {
        storage->SetFloat(warned_key, kNotWarned);
    }

    // Flag the value instead of hiding it: an amber field, explained on hover.
    if (out_of_range) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.55f, 0.35f, 0.05f, 0.65f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.65f, 0.42f, 0.08f, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.75f, 0.48f, 0.10f, 0.85f));
    }
    double shown = value / displayUnit;
    const bool edited = ImGui::InputDouble(label, &shown, minorStep, majorStep, format);
    if (out_of_range) {
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip |
                                 ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Outside the editable range %g to %g.\n"
                              "The component keeps this value until you edit the field.",
                              lower / displayUnit, upper / displayUnit);
    }

    if (!edited)
        return false;
    const std::optional<double> committed = boundedEdit(value, shown * displayUnit, lower, upper);
    if (!committed)
        return false;
    value = *committed;
    return true;
}

} // namespace detail

// Bounded numeric input. Shows `ref` as it is, even outside the limits (flagged
// and logged once), and changes it only when the user edits the field; see
// boundedEdit() for how an edit is clamped. Returns true when `ref` changed.
static bool inputDouble(std::string label, double &ref, double minorStep, double majorStep,
                        const char *format, double lowerLimit, double upperLimit) {
    return detail::boundedInput(label.c_str(), ref, minorStep, majorStep, format, lowerLimit,
                                upperLimit, 1.0);
}
// Frequency input in a caller-chosen display unit. `minorStep`/`majorStep` are
// expressed in that display unit, because ImGui::InputDouble steps whatever the
// field shows. `displayUnit_Hz` defaults to MHz, the panel-wide convention
// (`inputFrequency` returns the value in Hz, so no caller scales by hand). Same
// out-of-range and edit contract as inputDouble().
static bool inputFrequency(const char *label, double &freq_Hz, double minorStep, double majorStep,
                           const char *format, double lowerLimit_Hz, double upperLimit_Hz,
                           double displayUnit_Hz = 1e6) {
    return detail::boundedInput(label, freq_Hz, minorStep, majorStep, format, lowerLimit_Hz,
                                upperLimit_Hz, displayUnit_Hz);
}

} // namespace utils
