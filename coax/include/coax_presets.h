#pragma once

#include <array>

struct CableSpec {
    const char* name;          // e.g. "MT 340"
    double K1_dB_per_m;        // sqrt(f) coefficient, dB/m with f in MHz
    double K2_dB_per_m;        // f coefficient, dB/m with f in MHz
    double delay_ns_per_m;     // signal propagation delay
    double max_freq_GHz;       // upper limit of the datasheet model
    double diameter_mm;        // informational; for widget display
};

inline const std::array<CableSpec, 6> kCoaxCablePresets = {{
    // name,      K1,        K2,        delay, max_f,  diam
    {"MT 210",  0.0,       0.0,       0.0,   100.0,  0.0},  // TODO: populate from MT 210 datasheet
    {"MT 230",  0.0,       0.0,       0.0,   100.0,  0.0},  // TODO: populate from MT 230 datasheet
    {"MT 265",  0.0,       0.0,       0.0,   100.0,  0.0},  // TODO: populate from MT 265 datasheet
    {"MT 300",  0.0,       0.0,       0.0,   100.0,  0.0},  // TODO: populate from MT 300 datasheet
    {"MT 340",  0.004710,  0.000004,  4.76,  18.5,   8.6},
    {"MT 480",  0.0,       0.0,       0.0,   100.0,  0.0},  // TODO: populate from MT 480 datasheet
}};
