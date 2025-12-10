#include "spectrum_analyzer_engine.h"
#include "common.h"
#include "logging_core.h"
#include <cmath>
#include <random>

SpectrumAnalyzerEngine::SpectrumAnalyzerEngine() { LOG_INFO("Constructing spectrum engine..."); }

static double W_to_dBm(double w) {
    if (w <= 0.0) {
        return -174;
    }
    return 10.0 * std::log10(w) + 30.0;
}

double SpectrumAnalyzerEngine::generate_noise_power() {
    std::normal_distribution<double> dist(0.0, 1.0);
    std::random_device rd;
    std::mt19937 generator(rd());
    double noise = dist(generator);
    double rms_voltage = std::sqrt(4 * k * T * m_rbw * R);

    return std::pow(noise * rms_voltage, 2) / R;
}

std::vector<double> SpectrumAnalyzerEngine::integratePowerPerBin(const Spectrum &spec) const {
    size_t n = spec.frequencies.size();
    std::vector<double> power_W(n, 0.0);
    if (spec.noise_total_W.size() == n) {
        for (size_t i = 0; i < n; ++i) {
            power_W[i] = spec.noise_total_W[i];
        }
    } else if (!spec.noise_total_W.empty()) {
        for (size_t i = 0; i < n && i < spec.noise_total_W.size(); ++i) {
            power_W[i] = spec.noise_total_W[i];
        }
    }

    for (const auto &t : spec.tones) {
        if (n < 2) {
            continue;
        }
        double bin_width = spec.frequencies[1] - spec.frequencies[0];
        int bin_idx =
            static_cast<int>(std::round((t.freq_Hz - spec.frequencies.front()) / bin_width));
        if (bin_idx >= 0 && static_cast<size_t>(bin_idx) < n) {
            double tone_W = std::pow(10.0, (t.power_dBm - 30.0) / 10.0);
            power_W[bin_idx] += tone_W;
        }
    }

    return power_W;
}

std::vector<double> SpectrumAnalyzerEngine::renderSpectrum(const Spectrum &spec) const {

    if (spec.frequencies.empty()) {
        return {};
    }

    std::vector<double> power_W = this->integratePowerPerBin(spec);
    double bin_width = 1.0;
    if (spec.frequencies.size() >= 2) {
        bin_width = spec.frequencies[1] - spec.frequencies[0];
    }

    std::vector<double> power_dBm(power_W.size());
    for (size_t i = 0; i < power_W.size(); ++i) {
        power_dBm[i] = W_to_dBm(power_W[i]);
    }

    return power_dBm;
}
