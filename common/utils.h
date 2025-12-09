#pragma once

#include "logging_core.h"

namespace utils {
void inputDouble(std::string label, double &ref, double minorStep,
                 double majorStep, const char *format, double lowerLimit,
                 double upperLimit) {
    if (ref > upperLimit) {
        LOG_WARN("Unable to update value: %s! (above upper limit)",
                 label.c_str());
        ref = upperLimit;
    }

    if (ref < lowerLimit) {
        LOG_WARN("Unable to update value: %s! (below lower limit)",
                 label.c_str());
        ref = lowerLimit;
    }

    ImGui::InputDouble(label.c_str(), &ref, minorStep, majorStep, format);
}
} // namespace utils
