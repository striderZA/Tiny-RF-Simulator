#pragma once

#include <utility>
#include <vector>

typedef std::pair<double, double> tone;

constexpr double MIN_FREQ = -5.12e9;
constexpr double MAX_FREQ = 5.12e9;
constexpr size_t NUM_BINS = 1024;
constexpr double MIN_POWER = -174;
constexpr double MAX_POWER = 10;
constexpr double DEFAULT_VBW = 25e6;
constexpr double DEFAULT_RBW = 50e6;
constexpr double k = 1.3806e-23;
constexpr double T = 290.0;
constexpr double R = 50.0;

inline double dbToLinear(double dB) { return std::pow(10.0, db / 10.0); }
