#pragma once

#include <vector>

struct Spectrum {
	std::vector<double> frequencies;
	std::vector<double> signal_power_W;
	std::vector<double> noise_power_W;
};

// Shared constants (optional, can be moved to specific classes if preferred)
constexpr double MIN_FREQ = -20e9;
constexpr double MAX_FREQ = 20e9;
constexpr size_t NUM_BINS = 1024;
constexpr double MIN_POWER = -174;
constexpr double MAX_POWER = 10;
constexpr double DEFAULT_VBW = 10e6;
constexpr double DEFAULT_RBW = 50e6;
constexpr double k = 1.3806e-23;
constexpr double T = 290.0;
constexpr double R = 50.0;