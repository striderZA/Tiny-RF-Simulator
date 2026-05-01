#pragma once

#include <complex>
#include <vector>

struct IQStream {
    std::vector<std::complex<double>> samples;
    double sample_rate_Hz = 0.0;
};
