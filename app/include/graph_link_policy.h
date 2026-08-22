#pragma once

#include "component_interface.h"

inline bool graphLinkAllowed(const IComponentEngine *source, const IComponentEngine *target,
                             int start_pin, int end_pin) {
    if (!target || target->type_name() != "pfb")
        return true;

    return source && source->type_name() == "adc" && source->outputPinId(0) == start_pin &&
           target->inputPinId(0) == end_pin;
}
