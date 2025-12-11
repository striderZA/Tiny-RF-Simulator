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

    std::vector<double> rbw_power_W = this->applyRBW(power_W, bin_width);

    std::vector<double> power_dBm(rbw_power_W.size());
    for (size_t i = 0; i < rbw_power_W.size(); ++i) {
        power_dBm[i] = W_to_dBm(rbw_power_W[i]);
    }

    std::vector<double> vbw_out = this->applyVBW(power_dBm, bin_width);

    return vbw_out;
}

std::vector<double> SpectrumAnalyzerEngine::applyRBW(const std::vector<double> &power_W,
                                                     double binWidth) const {
    size_t n = power_W.size();
    if (n == 0) {
        return {};
    }
    // kernel width in bins (simple Gaussian approx)
    int kernel_half = std::max(1, static_cast<int>(std::round((m_rbw / binWidth) / 2.0)));
    int kernel_size = 2 * kernel_half + 1;

    std::vector<double> kernel(kernel_size);
    double sigma = kernel_half / 2.0 + 0.001;
    double sum = 0.0;

    for (int i = 0; i < kernel_size; ++i) {
        int x = i - kernel_half;
        kernel[i] = std::exp(-0.5 * (x * x) / (sigma * sigma));
        sum += kernel[i];
    }
    for (auto &k : kernel) {
        k /= sum;
    }

    std::vector<double> out(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        double acc = 0.0;
        for (int k = -kernel_half; k <= kernel_half; ++k) {
            int idx = static_cast<int>(i) + k;
            if (idx < 0 || idx >= static_cast<int>(n)) {
                continue;
            }
            acc += power_W[idx] * kernel[k + kernel_half];
        }
        out[i] = acc;
    }
    return out;
}

std::vector<double> SpectrumAnalyzerEngine::applyVBW(const std::vector<double> &power_dBm,
                                                     double binWidth) const {
    size_t n = power_dBm.size();

    if (n == 0) {
        return {};
    }

    int window = std::max(1, static_cast<int>(std::round(m_vbw / binWidth)));
    std::vector<double> out(n, 0.0);

    int half = window / 2;
    for (size_t i = 0; i < n; ++i) {
        double sum = 0.0;
        int count = 0;
        for (int k = -half; k <= half; ++k) {
            int idx = static_cast<int>(i) + k;
            if (idx < 0 || idx >= static_cast<int>(n)) {
                continue;
            }
            sum += power_dBm[idx];
            ++count;
        }
        out[i] = (count > 0) ? (sum / count) : power_dBm[i];
    }
    return out;
}
