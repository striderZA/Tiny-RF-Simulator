#pragma once

#include "imgui.h"
#include "logging_core.h"

namespace utils {
static bool inputDouble(std::string label, double &ref, double minorStep, double majorStep,
                        const char *format, double lowerLimit, double upperLimit) {
    // clamp external writes to ref BEFORE drawing widget
    if (ref > upperLimit) {
        LOG_WARN("Unable to update value: %s! (above upper limit)", label.c_str());
        ref = upperLimit;
    } else if (ref < lowerLimit) {
        LOG_WARN("Unable to update value: %s! (below lower limit)", label.c_str());
        ref = lowerLimit;
    }

    // perform ImGui update
    bool changed = ImGui::InputDouble(label.c_str(), &ref, minorStep, majorStep, format);

    // optionally clamp AFTER user change too
    if (ref > upperLimit) {
        ref = upperLimit;
        changed = true;
    }
    if (ref < lowerLimit) {
        ref = lowerLimit;
        changed = true;
    }

    return changed;
}
// Frequency input in a caller-chosen display unit. `minorStep`/`majorStep` are
// expressed in that display unit, because ImGui::InputDouble steps whatever the
// field shows. `displayUnit_Hz` defaults to MHz, the panel-wide convention
// (`inputFrequency` returns the value in Hz, so no caller scales by hand).
static bool inputFrequency(const char *label, double &freq_Hz, double minorStep, double majorStep,
                           const char *format, double lowerLimit_Hz, double upperLimit_Hz,
                           double displayUnit_Hz = 1e6) {
    // clamp external writes to freq_Hz BEFORE drawing widget
    if (freq_Hz > upperLimit_Hz) {
        LOG_WARN("Unable to update frequency: %s! (above upper limit)", label);
        freq_Hz = upperLimit_Hz;
    } else if (freq_Hz < lowerLimit_Hz) {
        LOG_WARN("Unable to update frequency: %s! (below lower limit)", label);
        freq_Hz = lowerLimit_Hz;
    }

    double displayValue = freq_Hz / displayUnit_Hz;
    bool changed = ImGui::InputDouble(label, &displayValue, minorStep, majorStep, format);

    if (changed) {
        freq_Hz = displayValue * displayUnit_Hz;
        // optionally clamp AFTER user change too
        if (freq_Hz > upperLimit_Hz) {
            LOG_WARN("Unable to update frequency: %s! (above upper limit)", label);
            freq_Hz = upperLimit_Hz;
        } else if (freq_Hz < lowerLimit_Hz) {
            LOG_WARN("Unable to update frequency: %s! (below lower limit)", label);
            freq_Hz = lowerLimit_Hz;
        }
    }
    return changed;
}

} // namespace utils
