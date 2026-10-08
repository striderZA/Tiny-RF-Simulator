#pragma once

#include <optional>

class IComponentEngine;
class SpectrumAnalyzerEngine;

enum class SnrBasis { Rbw, PfbChannel };

struct OutputSnr {
    std::optional<double> snr_dB;
    SnrBasis basis = SnrBasis::Rbw;
    double rbw_Hz = 0.0;                     // RBW used when basis is Rbw.
    double enbw_Hz = 0.0;                    // Active PFB channel ENBW when basis is PfbChannel.
    std::optional<double> channel_noise_dBm; // Integrated active-channel noise, when available.
};

// Uses integrated active-channel noise for PFB output 0. Other available
// outputs use the analyzer's current RBW; unavailable outputs return no SNR.

OutputSnr computeOutputSnr(const IComponentEngine &component, int output_port,
                           const SpectrumAnalyzerEngine &analyzer);
