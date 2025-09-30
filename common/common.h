#pragma once

#include <vector>

struct Spectrum {
	std::vector<double> frequencies;
	std::vector<double> signal;
	std::vector<double> noise;
};

// Shared constants (optional, can be moved to specific classes if preferred)
constexpr double MIN_FREQ = -100;
constexpr double MAX_FREQ = 100.0;
constexpr size_t NUM_BINS = 1024;