#pragma once

#include "spectrum.h"

struct SignalNode {
    std::vector<const Spectrum *> inputs;
    std::vector<Spectrum> outputs;
    bool view_enabled = false;
};
