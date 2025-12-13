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
} // namespace utils
