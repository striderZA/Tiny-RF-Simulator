#pragma once

#include "spectrum.h"
#include <limits>

enum class PowerMeterError {
    None,
    MissingSource,
    InvalidFrequencyGrid,
    InvalidNoise,
    InvalidTone,
};

struct PowerMeasurement {
    bool valid = false;
    double power_dBm = std::numeric_limits<double>::quiet_NaN();
    PowerMeterError error = PowerMeterError::MissingSource;
};

const char *powerMeterErrorMessage(PowerMeterError error);

class PowerMeterEngine {
  public:
    PowerMeasurement measure(const Spectrum *source) const;
};
