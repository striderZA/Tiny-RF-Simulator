#pragma once

#include "spectrum.h"

struct SignalNode {
    Spectrum input;
    Spectrum output;
    bool view_enabled = false;
};
