#pragma once

#include "imgui.h"
#include "receiver_requirements.h"
#include "receiver_requirements_widget.h"

#include <cmath>
#include <cstddef>
#include <cstdio>

// Shared presentation primitives for the Receiver Requirements and RF Budget views. Everything
// here is non-interactive: a badge or a hint consumes a fixed amount of space and submits no
// focusable item, so window scrolling, keyboard navigation, and scripted item lookup stay
// predictable.
namespace rf_view_ui {

inline ImVec4 passColor() { return ImVec4(0.25f, 0.85f, 0.35f, 1.0f); }
inline ImVec4 failColor() { return ImVec4(1.00f, 0.35f, 0.35f, 1.0f); }
inline ImVec4 warnColor() { return ImVec4(1.00f, 0.78f, 0.25f, 1.0f); }

inline const char *statusText(ReceiverRequirementStatus status) {
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

inline ImVec4 statusColor(ReceiverRequirementStatus status) {
    switch (receiverRequirementStatusTone(status)) {
    case ReceiverRequirementStatusTone::PassGreen:
        return passColor();
    case ReceiverRequirementStatusTone::FailRed:
        return failColor();
    case ReceiverRequirementStatusTone::Neutral:
        return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    }
    return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
}

// A pill-shaped label. The text carries the meaning, so the colour is reinforcement only.
inline void tag(const char *text, const ImVec4 &color, bool dot) {
    const ImVec2 text_size = ImGui::CalcTextSize(text);
    const float pad_x = 6.0f;
    const float pad_y = 2.0f;
    const float dot_width = dot ? 8.0f : 0.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(text_size.x + 2.0f * pad_x + dot_width, text_size.y + 2.0f * pad_y);
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    const float rounding = size.y * 0.5f;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImU32 fill = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, color.w * 0.16f));
    const ImU32 edge = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, color.w * 0.55f));
    draw->AddRectFilled(pos, max, fill, rounding);
    draw->AddRect(pos, max, edge, rounding);
    if (dot)
        draw->AddCircleFilled(
            ImVec2(pos.x + pad_x + dot_width * 0.5f - 1.0f, pos.y + size.y * 0.5f), 2.5f,
            ImGui::GetColorU32(color));
    draw->AddText(ImVec2(pos.x + pad_x + dot_width, pos.y + pad_y), ImGui::GetColorU32(color),
                  text);
    ImGui::Dummy(size);
}

// A status pill; a tooltip is shown on hover when `tooltip` is non-null.
inline void statusBadge(ReceiverRequirementStatus status, const char *tooltip = nullptr) {
    const bool dot =
        status == ReceiverRequirementStatus::Pass || status == ReceiverRequirementStatus::Fail;
    tag(statusText(status), statusColor(status), dot);
    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
}

// The observed range of a metric, or a dash when nothing was measured.
inline void observedText(const ReceiverMetricEvaluation &metric, const char *unit) {
    if (!metric.observed_min || !metric.observed_max) {
        ImGui::TextDisabled("-");
        return;
    }
    if (*metric.observed_min == *metric.observed_max)
        ImGui::Text("%.3f %s", *metric.observed_min, unit);
    else
        ImGui::Text("%.3f to %.3f %s", *metric.observed_min, *metric.observed_max, unit);
}

// A status pill that also explains its worst case on hover.
inline void metricStatus(const ReceiverMetricEvaluation &metric) {
    char tooltip[96] = {};
    if (metric.observed_min && metric.observed_max) {
        if (*metric.observed_min == *metric.observed_max)
            std::snprintf(tooltip, sizeof(tooltip), "Worst case: %.3f", *metric.observed_min);
        else
            std::snprintf(tooltip, sizeof(tooltip), "Worst case: %.3f to %.3f",
                          *metric.observed_min, *metric.observed_max);
    }
    statusBadge(metric.status, tooltip[0] ? tooltip : nullptr);
}

inline void sectionHeader(const char *label) {
    ImGui::Spacing();
    ImGui::SeparatorText(label);
    ImGui::Spacing();
}

inline void hint(const char *text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

// Always consumes one text line, so a line that appears and disappears never shifts the
// controls below it.
inline void reservedLine(const char *text) {
    if (text && *text)
        ImGui::TextDisabled("%s", text);
    else
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
}

// "%.2f" for a finite value; infinities are real budget results, not blanks.
inline void formatSeriesValue(double value, char *buffer, std::size_t size) {
    if (std::isinf(value))
        std::snprintf(buffer, size, "%s", value > 0.0 ? "+inf" : "-inf");
    else
        std::snprintf(buffer, size, "%.2f", value);
}

} // namespace rf_view_ui
