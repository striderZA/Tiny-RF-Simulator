#pragma once

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

    void computeTotalNoise() {
        size_t n = std::max(noise_W.size(), noise_added_W.size());
        noise_total_W.assign(n, 0.0);

        for (size_t i = 0; i < n; ++i) {
            double a = (i < noise_W.size()) ? noise_W[i] : 0.0;
            double b = (i < noise_added_W.size()) ? noise_added_W[i] : 0.0;
            noise_total_W[i] = a + b;
        }
    }
};
