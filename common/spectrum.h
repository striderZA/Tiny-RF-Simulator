#pragma once

#include "common.h"
#include <cmath>
#include <random>
#include <vector>

struct Spectrum {
    struct Tone {
        double freq_Hz = 0.0;
        double power_dBm = -174;
    };

    std::vector<double> frequencies;
    std::vector<Tone> tones;
    std::vector<double> noise_W;
    std::vector<double> noise_added_W;
    std::vector<double> noise_total_W;

    double generateNoisePower() {
        std::normal_distribution<double> dist(0.0, 1.0);
        std::random_device rd;
        std::mt19937 generator(rd());
        double noise = dist(generator);
        double rms_voltage = std::sqrt(4 * k * T * R);

        return std::pow(noise * rms_voltage, 2) / R;
    }

    void computeTotalNoise() {
        size_t n = std::max(noise_W.size(), noise_added_W.size());
        noise_total_W.assign(n, 0.0);

        for (size_t i = 0; i < n; ++i) {
            noise_total_W[i] = generateNoisePower();
        }
    }
};
