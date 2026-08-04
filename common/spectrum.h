#pragma once

#include "common.h"
#include <cmath>
#include <cstdint>
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
    std::vector<double> noise_W;       // input noise density (W/Hz)
    std::vector<double> noise_added_W; // added noise density (W/Hz)
    std::vector<double> noise_total_W; // total output noise density (W/Hz)

    // Phase (degrees) per frequency bin, same size as frequencies.
    std::vector<double> phase_deg;

    // Sample rate of the signal this spectrum represents (Hz).
    // Set by ADCs, propagated through most components, read by PFB channelizer.
    double fs_Hz = 0.0;

    // True for spectra downstream of an ADC's DDC (complex baseband/IQ). False (default) for
    // real-domain (analog) spectra. Set only by AdcEngine's output; propagated downstream from
    // there exactly like fs_Hz already is. Determines whether the spectrum-analyzer render path
    // needs to apply conjugateSymmetricExpand() before binning tone power (see that function).
    bool is_complex_baseband = false;

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

// Expands real-domain tones into their conjugate-symmetric (+-fc) representation per Euler's
// formula: cos(2*pi*fc*t) = 0.5*(exp(j*2*pi*fc*t) + exp(-j*2*pi*fc*t)). Each non-DC tone becomes
// two entries at +freq_Hz and -freq_Hz, each at half the linear power (-3.0103 dB = 10*log10(2)
// below the input). DC tones (freq_Hz == 0) are self-conjugate and pass through unchanged (no
// mirror, no power split).
//
// Used only where +-fc content is physically meaningful: rendering a real-domain (pre-ADC)
// spectrum. NOT used by interior DSP (generator, nonlinear_model.h, gain/filter/S-param stages,
// mixer), which must stay on the collapsed single-entry-per-tone representation — splitting there
// would corrupt nonlinear_model.h's harmonic/IM math (calibrated against full real-tone power).
inline std::vector<Spectrum::Tone>
conjugateSymmetricExpand(const std::vector<Spectrum::Tone> &tones) {
    std::vector<Spectrum::Tone> out;
    out.reserve(tones.size() * 2);
    for (const auto &t : tones) {
        if (t.freq_Hz == 0.0) {
            out.push_back(t);
            continue;
        }
        Spectrum::Tone half = t;
        half.power_dBm = t.power_dBm - 10.0 * std::log10(2.0);
        Spectrum::Tone at_f = half;
        at_f.freq_Hz = t.freq_Hz;
        Spectrum::Tone at_negf = half;
        at_negf.freq_Hz = -t.freq_Hz;
        out.push_back(at_f);
        out.push_back(at_negf);
    }
    return out;
}

struct Peak {
    int index;
    double freq_Hz;
    double power_dBm;
};
