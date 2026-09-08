#include "power_meter_engine.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double kFrequencyTolerance = 1e-12;

bool nearlyEqual(double left, double right) {
    const double scale = std::max({1.0, std::abs(left), std::abs(right)});
    return std::abs(left - right) <= kFrequencyTolerance * scale;
}

PowerMeasurement invalid(PowerMeterError error) {
    return {false, std::numeric_limits<double>::quiet_NaN(), error};
}
} // namespace

const char *powerMeterErrorMessage(PowerMeterError error) {
    switch (error) {
    case PowerMeterError::None:
        return "none";
    case PowerMeterError::MissingSource:
        return "no source selected";
    case PowerMeterError::InvalidFrequencyGrid:
        return "invalid frequency grid";
    case PowerMeterError::InvalidNoise:
        return "invalid noise data";
    case PowerMeterError::InvalidTone:
        return "invalid tone data";
    }
    return "invalid measurement";
}

PowerMeasurement PowerMeterEngine::measure(const Spectrum *source) const {
    if (!source)
        return invalid(PowerMeterError::MissingSource);

    const auto &frequencies = source->frequencies;
    if (frequencies.size() < 2)
        return invalid(PowerMeterError::InvalidFrequencyGrid);

    const double bin_width = frequencies[1] - frequencies[0];
    if (!std::isfinite(bin_width) || bin_width <= 0.0)
        return invalid(PowerMeterError::InvalidFrequencyGrid);

    for (size_t i = 0; i < frequencies.size(); ++i) {
        if (!std::isfinite(frequencies[i]) || (i > 0 && frequencies[i] <= frequencies[i - 1]))
            return invalid(PowerMeterError::InvalidFrequencyGrid);
        if (i > 1 && !nearlyEqual(frequencies[i] - frequencies[i - 1], bin_width))
            return invalid(PowerMeterError::InvalidFrequencyGrid);
    }

    double total_watts = 0.0;
    for (const auto &tone : source->tones) {
        if (!std::isfinite(tone.freq_Hz) || !std::isfinite(tone.power_dBm) ||
            !std::isfinite(tone.phase_deg))
            return invalid(PowerMeterError::InvalidTone);

        total_watts += std::pow(10.0, (tone.power_dBm - 30.0) / 10.0);
        if (!std::isfinite(total_watts))
            return invalid(PowerMeterError::InvalidTone);
    }

    if (!source->noise_total_W.empty()) {
        if (source->noise_total_W.size() != frequencies.size())
            return invalid(PowerMeterError::InvalidNoise);

        for (double density : source->noise_total_W) {
            if (!std::isfinite(density) || density < 0.0)
                return invalid(PowerMeterError::InvalidNoise);
            total_watts += density * bin_width;
            if (!std::isfinite(total_watts))
                return invalid(PowerMeterError::InvalidNoise);
        }
    }

    if (total_watts == 0.0)
        return {true, -std::numeric_limits<double>::infinity(), PowerMeterError::None};
    return {true, 10.0 * std::log10(total_watts) + 30.0, PowerMeterError::None};
}
