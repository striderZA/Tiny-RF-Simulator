#pragma once

#include "common.h"
#include <cmath>
#include <chrono>
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

    double thermalNoisePower_W(double bin_width) {
        double mean_noise_power_W = k * T * bin_width;
        static thread_local std::mt19937 gen = []() {
            std::random_device rd;
            try {
                return std::mt19937(rd());
            } catch (...) {
                return std::mt19937(std::chrono::system_clock::now().time_since_epoch().count());
            }
        }();
        std::normal_distribution<double> dist(mean_noise_power_W, 0.5 * mean_noise_power_W);

        double v = dist(gen);
        if (v < 0) {
            v = 0;
        }
        return v;
    }

    void computeTotalNoise() {
        size_t n = frequencies.size();
        noise_total_W.assign(n, 0.0);
        if (n < 2) {
            return;
        }
        double bin_width = frequencies[1] - frequencies[0];
        for (size_t i = 0; i < n; ++i) {
            double noise_input = (i < noise_W.size()) ? noise_W[i] : 0.0;
            double noise_added = (i < noise_added_W.size()) ? noise_added_W[i] : 0.0;
            double awgn = thermalNoisePower_W(bin_width);

            noise_total_W[i] = noise_input + noise_added + awgn;
        }
    }
};

struct Peak {
    int index;
    double freq_Hz;
    double power_dBm;
};
