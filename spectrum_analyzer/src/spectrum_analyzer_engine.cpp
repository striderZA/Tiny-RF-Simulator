#include "spectrum_analyzer_engine.h"
#include "logging_core.h"
#include <algorithm>
#include <cmath>
#include <random>

SpectrumAnalyzerEngine::SpectrumAnalyzerEngine() : m_rng(std::random_device{}()) {
    LOG_INFO("Constructing spectrum engine...");
}

static double W_to_dBm(double w) {
    if (w <= 0.0) {
        return -174;
    }
    return 10.0 * std::log10(w) + 30.0;
}

std::vector<double> SpectrumAnalyzerEngine::integratePowerPerBin(const Spectrum &spec) const {
    size_t n = spec.frequencies.size();
    std::vector<double> power_W(n, 0.0);

    double bin_width = 1.0;
    if (spec.frequencies.size() >= 2) {
        bin_width = spec.frequencies[1] - spec.frequencies[0];
    }

    // Convert noise density (W/Hz) to per-bin power (W)
    if (spec.noise_total_W.size() == n) {
        for (size_t i = 0; i < n; ++i) {
            power_W[i] = spec.noise_total_W[i] * bin_width;
        }
    } else if (!spec.noise_total_W.empty()) {
        for (size_t i = 0; i < n && i < spec.noise_total_W.size(); ++i) {
            power_W[i] = spec.noise_total_W[i] * bin_width;
        }
    }

    // Add tones as discrete impulses
    for (const auto &t : spec.tones) {
        if (n < 2) {
            continue;
        }
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

    double bin_width = 1.0;
    if (spec.frequencies.size() >= 2) {
        bin_width = spec.frequencies[1] - spec.frequencies[0];
    }

    // Cache the expensive integrate + RBW step. Jitter + VBW still run every
    // frame so the display keeps its instrument-like live feel.
    std::vector<double> rbw_power_W;
    if (&spec == m_cache_spectrum && spec.generation == m_cache_spec_gen &&
        m_rbw == m_cache_rbw && bin_width == m_cache_bin_width) {
        rbw_power_W = m_cache_rbw_power_W;
    } else {
        std::vector<double> power_W = this->integratePowerPerBin(spec);
        rbw_power_W = this->applyRBW(power_W, bin_width);
        m_cache_spectrum = &spec;
        m_cache_spec_gen = spec.generation;
        m_cache_rbw = m_rbw;
        m_cache_bin_width = bin_width;
        m_cache_rbw_power_W = rbw_power_W;
    }

    std::vector<double> power_dBm(rbw_power_W.size());
    for (size_t i = 0; i < rbw_power_W.size(); ++i) {
        power_dBm[i] = W_to_dBm(rbw_power_W[i]);
    }

    if (m_noise_jitter_enabled && m_noise_jitter_sigma_dB > 0.0) {
        std::normal_distribution<double> jitter(0.0, m_noise_jitter_sigma_dB);
        for (auto &p : power_dBm) {
            p += jitter(m_rng);
        }
    }

    std::vector<double> vbw_out = this->applyVBW(power_dBm, bin_width);
    return this->applyTraceMode(spec, vbw_out);
}

std::vector<double>
SpectrumAnalyzerEngine::renderCombinedSpectrum(const std::vector<const Spectrum *> &specs) const {
    if (specs.empty()) {
        return {};
    }

    // Determine target length (use largest frequency vector among inputs)
    size_t n = 0;
    for (const Spectrum *s : specs) {
        if (s && s->frequencies.size() > n) {
            n = s->frequencies.size();
        }
    }
    if (n == 0) {
        return {};
    }

    // Sum per-bin power (W). Use integratePowerPerBin for each Spectrum and add.
    std::vector<double> sum_power_W(n, 0.0);
    for (const Spectrum *s : specs) {
        if (!s) {
            continue;
        }
        std::vector<double> p = this->integratePowerPerBin(*s);
        size_t m = std::min(n, p.size());
        for (size_t i = 0; i < m; ++i) {
            sum_power_W[i] += p[i];
        }
    }

    // Pick a sensible bin width from the first non-empty frequency vector
    double bin_width = 1.0;
    for (const Spectrum *s : specs) {
        if (s && s->frequencies.size() >= 2) {
            bin_width = s->frequencies[1] - s->frequencies.front();
            break;
        }
    }

    std::vector<double> rbw_power_W = this->applyRBW(sum_power_W, bin_width);

    std::vector<double> power_dBm(rbw_power_W.size());
    for (size_t i = 0; i < rbw_power_W.size(); ++i) {
        power_dBm[i] = W_to_dBm(rbw_power_W[i]);
    }

    // Add random noise jitter in dBm domain for visual "live" spectrum effect.
    // Symmetric in dBm — no asymmetric clamping artifacts like power-domain jitter.
    // VBW smooths this naturally; tone peaks are unaffected.
    if (m_noise_jitter_enabled && m_noise_jitter_sigma_dB > 0.0) {
        std::normal_distribution<double> jitter(0.0, m_noise_jitter_sigma_dB);
        for (auto &p : power_dBm) {
            p += jitter(m_rng);
        }
    }

    std::vector<double> vbw_out = this->applyVBW(power_dBm, bin_width);
    return vbw_out;
}

double SpectrumAnalyzerEngine::computeAverageNoiseLevel(
    const std::vector<const Spectrum *> &specs) const {
    if (specs.empty()) {
        return -174.0;
    }

    size_t n = 0;
    for (const Spectrum *s : specs) {
        if (s && s->frequencies.size() > n) {
            n = s->frequencies.size();
        }
    }
    if (n == 0) {
        return -174.0;
    }

    double bin_width = 1.0;
    for (const Spectrum *s : specs) {
        if (s && s->frequencies.size() >= 2) {
            bin_width = s->frequencies[1] - s->frequencies.front();
            break;
        }
    }

    // Sum noise power per bin (W) across all spectra, excluding tones
    std::vector<double> sum_noise_W(n, 0.0);
    for (const Spectrum *s : specs) {
        if (!s) {
            continue;
        }
        size_t m = std::min(n, s->noise_total_W.size());
        for (size_t i = 0; i < m; ++i) {
            sum_noise_W[i] += s->noise_total_W[i] * bin_width;
        }
    }

    std::vector<double> rbw_noise_W = this->applyRBW(sum_noise_W, bin_width);
    if (rbw_noise_W.empty()) {
        return -174.0;
    }

    double sum_dBm = 0.0;
    for (double w : rbw_noise_W) {
        sum_dBm += W_to_dBm(w);
    }
    return sum_dBm / static_cast<double>(rbw_noise_W.size());
}

std::vector<Peak> SpectrumAnalyzerEngine::findPeaks(const std::vector<double> &power_dBm,
                                                    const std::vector<double> &freq_axis,
                                                    size_t max_count) const {
    std::vector<Peak> peaks;
    size_t n = power_dBm.size();
    if (n < 3 || freq_axis.size() != n) {
        return peaks;
    }

    for (size_t i = 1; i + 1 < n; ++i) {
        if (power_dBm[i] > power_dBm[i - 1] && power_dBm[i] > power_dBm[i + 1]) {
            peaks.push_back(Peak{static_cast<int>(i), freq_axis[i], power_dBm[i]});
        }
    }

    std::sort(peaks.begin(), peaks.end(),
              [](const Peak &a, const Peak &b) { return a.power_dBm > b.power_dBm; });

    if (peaks.size() > max_count) {
        peaks.resize(max_count);
    }
    return peaks;
}

std::vector<double> SpectrumAnalyzerEngine::applyRBW(const std::vector<double> &power_W,
                                                     double binWidth) const {
    size_t n = power_W.size();
    if (n == 0) {
        return {};
    }
    // Gaussian RBW filter. Scale sigma so that the discrete sum of the kernel
    // approximates RBW / binWidth. For flat noise density D (W/Hz), each bin
    // holds D * binWidth (W). After convolution: output = D * binWidth * sum(kernel)
    //                          ≈ D * binWidth * (RBW / binWidth) = D * RBW
    // This makes the displayed noise floor independent of the internal grid spacing.
    // For a tone (single-bin impulse), peak power is preserved because kernel[center]=1.
    double rbw_bins = m_rbw / binWidth;
    constexpr double sqrt_2pi = 2.5066282746310002; // sqrt(2 * pi)
    double sigma = rbw_bins / sqrt_2pi;
    int kernel_half = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
    int kernel_size = 2 * kernel_half + 1;

    std::vector<double> kernel(kernel_size);
    for (int i = 0; i < kernel_size; ++i) {
        int x = i - kernel_half;
        kernel[i] = std::exp(-0.5 * (x * x) / (sigma * sigma));
    }
    // Kernel peaks at 1 (center) — preserves tone peak power.
    // sum(kernel) ≈ RBW / binWidth — integrates noise over RBW, grid-independent.

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

std::vector<double> SpectrumAnalyzerEngine::applyTraceMode(
    const Spectrum &spec, const std::vector<double> &after_vbw) const
{
    if (m_trace_mode == TraceMode::ClearWrite) {
        return after_vbw;
    }

    std::unordered_map<const Spectrum*, std::vector<double>>* history = nullptr;
    if (m_trace_mode == TraceMode::MaxHold) history = &m_max_hold;
    // MinHold and VideoAverage will be added in subsequent tasks
    if (!history) return after_vbw;

    auto& h = (*history)[&spec];

    if (h.size() != after_vbw.size()) {
        h = after_vbw;
        return after_vbw;
    }

    if (m_trace_mode == TraceMode::MaxHold) {
        for (size_t i = 0; i < h.size(); ++i)
            h[i] = std::max(h[i], after_vbw[i]);
    }
    return h;
}
