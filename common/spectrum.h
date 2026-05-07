#pragma once

#include "common.h"
#include <cmath>
#include <vector>

struct Spectrum {
    struct Tone {
        double freq_Hz = 0.0;
        double power_dBm = -174;
        double phase_deg = 0.0;
    };

    std::vector<double> frequencies;
    std::vector<Tone> tones;

    // Noise vectors store POWER SPECTRAL DENSITY in W/Hz.
    // To get total power in a bin, multiply by bin width.
    std::vector<double> noise_W;        // input noise density (W/Hz)
    std::vector<double> noise_added_W;  // added noise density (W/Hz)
    std::vector<double> noise_total_W;  // total output noise density (W/Hz)

    // Phase (degrees) per frequency bin, same size as frequencies.
    std::vector<double> phase_deg;

    // Sample rate of the signal this spectrum represents (Hz).
    // Set by ADCs, propagated through most components, read by PFB channelizer.
    double fs_Hz = 0.0;

    // Generation counter for dirty-flag tracking. Producers increment after
    // recomputation so consumers can detect upstream changes.
    uint64_t generation = 0;

    void bumpGeneration() { ++generation; }

    void computeTotalNoise() {
        size_t n = frequencies.size();
        noise_total_W.assign(n, 0.0);
        if (n < 2) {
            return;
        }
        for (size_t i = 0; i < n; ++i) {
            double noise_input = (i < noise_W.size()) ? noise_W[i] : 0.0;
            double noise_added = (i < noise_added_W.size()) ? noise_added_W[i] : 0.0;
            noise_total_W[i] = noise_input + noise_added;
        }

    }
};

struct Peak {
    int index;
    double freq_Hz;
    double power_dBm;
};
