#pragma once

#include <vector>

struct Spectrum {
	std::vector<double> frequencies;
	std::vector<double> signal;
	std::vector<double> noise;
};

// Shared constants (optional, can be moved to specific classes if preferred)
constexpr double MIN_FREQ = -5000;
constexpr double MAX_FREQ = 5000;
constexpr size_t NUM_BINS = 1024;
constexpr double MIN_POWER = -174;
constexpr double MAX_POWER = 10;
