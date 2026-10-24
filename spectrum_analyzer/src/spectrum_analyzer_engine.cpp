#include "spectrum_analyzer_engine.h"
#include "logging_core.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <random>
#include <unordered_set>

SpectrumAnalyzerEngine::SpectrumAnalyzerEngine() : m_rng(std::random_device{}()) {
    LOG_INFO("Constructing spectrum engine...");
}

static double W_to_dBm(double w) {
    if (w <= 0.0) {
        return -174;
    }
    return 10.0 * std::log10(w) + 30.0;
}

namespace {

// Bin width of a grid: its first spacing, or 1 for a single-bin grid.
double gridBinWidth(const std::vector<double> &freqs) {
    return freqs.size() >= 2 ? freqs[1] - freqs[0] : 1.0;
}

// Whether x lies within a grid's span, padded by half a bin on each side.
bool withinGrid(const std::vector<double> &freqs, double bin_width, double x) {
    return x >= freqs.front() - bin_width / 2.0 && x <= freqs.back() + bin_width / 2.0;
}

// Index of the grid entry nearest x; ties resolve to the lower index.
size_t nearestGridIndex(const std::vector<double> &freqs, double x) {
    auto hi_it = std::lower_bound(freqs.begin(), freqs.end(), x);
    if (hi_it == freqs.begin()) {
        return 0;
    }
    if (hi_it == freqs.end()) {
        return freqs.size() - 1;
    }
    size_t hi = static_cast<size_t>(hi_it - freqs.begin());
    size_t lo = hi - 1;
    return (x - freqs[lo] <= freqs[hi] - x) ? lo : hi;
}

// Adds one trace's per-bin noise (W) and tone (W) onto readout-axis bins. Noise is a
// density, so each axis bin samples the nearest source bin and is rescaled to the axis bin
// width. A tone is an impulse, so its power moves to the nearest axis bin. On an identical
// grid this is the identity. A null sum_tone_W skips tones.
void accumulateOntoGrid(const std::vector<double> &src_freqs,
                        const std::vector<double> &src_noise_W,
                        const std::vector<double> &src_tone_W, const std::vector<double> &grid,
                        double grid_bin_width, std::vector<double> &sum_noise_W,
                        std::vector<double> *sum_tone_W) {
    const double src_bin_width = gridBinWidth(src_freqs);
    const double noise_scale = grid_bin_width / src_bin_width;
    for (size_t j = 0; j < grid.size(); ++j) {
        if (!withinGrid(src_freqs, src_bin_width, grid[j])) {
            continue;
        }
        sum_noise_W[j] += src_noise_W[nearestGridIndex(src_freqs, grid[j])] * noise_scale;
    }

    if (!sum_tone_W) {
        return;
    }
    for (size_t i = 0; i < src_freqs.size(); ++i) {
        if (src_tone_W[i] == 0.0 || !withinGrid(grid, grid_bin_width, src_freqs[i])) {
            continue;
        }
        (*sum_tone_W)[nearestGridIndex(grid, src_freqs[i])] += src_tone_W[i];
    }
}

} // namespace

std::vector<double>
SpectrumAnalyzerEngine::combinedReadoutAxis(const std::vector<const Spectrum *> &specs) const {
    // Common axis for every trace: from the lowest start to the highest stop, at the coarsest
    // bin spacing among the traces. Each trace is sampled onto it, so no trace's bins fall
    // outside the axis and the result does not depend on trace order.
    double start = 0.0;
    double stop = 0.0;
    double step = 0.0;
    bool found = false;
    for (const Spectrum *s : specs) {
        if (!s || s->frequencies.empty()) {
            continue;
        }
        const std::vector<double> &f = s->frequencies;
        start = found ? std::min(start, f.front()) : f.front();
        stop = found ? std::max(stop, f.back()) : f.back();
        found = true;
        if (f.size() >= 2 && gridBinWidth(f) > step) {
            step = gridBinWidth(f);
        }
    }
    if (!found) {
        return {};
    }
    if (!(step > 0.0) || !std::isfinite(step)) {
        step = 1.0;
    }
    if (!(stop > start)) {
        return {start};
    }
    const size_t n = static_cast<size_t>(std::floor((stop - start) / step + 1e-9)) + 1;
    std::vector<double> axis(n);
    for (size_t i = 0; i < n; ++i) {
        axis[i] = start + static_cast<double>(i) * step;
    }
    return axis;
}

void SpectrumAnalyzerEngine::binPowerComponents(const Spectrum &spec, std::vector<double> &noise_W,
                                                std::vector<double> &tone_W) const {
    size_t n = spec.frequencies.size();
    noise_W.assign(n, 0.0);
    tone_W.assign(n, 0.0);

    double bin_width = 1.0;
    if (spec.frequencies.size() >= 2) {
        bin_width = spec.frequencies[1] - spec.frequencies[0];
    }

    // Convert noise density (W/Hz) to per-bin power (W)
    if (spec.noise_total_W.size() == n) {
        for (size_t i = 0; i < n; ++i) {
            noise_W[i] = spec.noise_total_W[i] * bin_width;
        }
    } else if (!spec.noise_total_W.empty()) {
        for (size_t i = 0; i < n && i < spec.noise_total_W.size(); ++i) {
            noise_W[i] = spec.noise_total_W[i] * bin_width;
        }
    }

    // Tones are discrete impulses. Real-domain spectra (pre-ADC) get expanded into their
    // +-fc conjugate-symmetric half-power pair per Euler's formula before binning; post-ADC
    // complex-baseband spectra are already correctly one-sided and render as-is.
    std::vector<Spectrum::Tone> expanded_tones;
    const std::vector<Spectrum::Tone> *display_tones = &spec.tones;
    if (!spec.is_complex_baseband) {
        expanded_tones = conjugateSymmetricExpand(spec.tones);
        display_tones = &expanded_tones;
    }
    if (n < 2) {
        return;
    }
    for (const auto &[freq_Hz, watts] : coherentToneWatts(*display_tones)) {
        int bin_idx =
            static_cast<int>(std::round((freq_Hz - spec.frequencies.front()) / bin_width));
        if (bin_idx >= 0 && static_cast<size_t>(bin_idx) < n) {
            tone_W[bin_idx] += watts;
        }
    }
}

std::vector<double>
SpectrumAnalyzerEngine::toJitteredNoise_dBm(const std::vector<double> &noise_W) const {
    std::vector<double> noise_dBm(noise_W.size());
    // Constructing the distribution is only defined for a positive sigma, so
    // guard it: a disabled or invalid setting stays a pure no-op.
    if (m_noise_jitter_enabled && m_noise_jitter_sigma_dB > 0.0) {
        std::normal_distribution<double> jitter(0.0, m_noise_jitter_sigma_dB);
        for (size_t i = 0; i < noise_dBm.size(); ++i) {
            noise_dBm[i] = W_to_dBm(noise_W[i] * std::pow(10.0, jitter(m_rng) / 10.0));
        }
    } else {
        for (size_t i = 0; i < noise_dBm.size(); ++i) {
            noise_dBm[i] = W_to_dBm(noise_W[i]);
        }
    }
    return noise_dBm;
}

std::vector<double>
SpectrumAnalyzerEngine::toDisplayPower_dBm(const std::vector<double> &noise_W,
                                           const std::vector<double> &tone_W,
                                           double binWidth) const {
    // VBW smooths the noise floor only: the moving average runs on noise dBm, and tone
    // power is added back in linear watts afterwards so a tone peak is never averaged.
    const std::vector<double> noise_dBm =
        this->applyVBW(this->toJitteredNoise_dBm(noise_W), binWidth);
    std::vector<double> power_dBm(noise_dBm.size());
    for (size_t i = 0; i < power_dBm.size(); ++i) {
        if (tone_W[i] > 0.0) {
            power_dBm[i] = W_to_dBm(std::pow(10.0, (noise_dBm[i] - 30.0) / 10.0) + tone_W[i]);
        } else {
            power_dBm[i] = noise_dBm[i];
        }
    }
    return power_dBm;
}

std::vector<double> SpectrumAnalyzerEngine::renderSpectrum(const Spectrum &spec) const {

    if (spec.frequencies.empty()) {
        return {};
    }

    double bin_width = 1.0;
    if (spec.frequencies.size() >= 2) {
        bin_width = spec.frequencies[1] - spec.frequencies[0];
    }

    // Cache the expensive split + RBW step. Jitter + VBW still run every frame
    // so the display keeps its instrument-like live feel.
    std::vector<double> rbw_noise_W, rbw_tone_W;
    if (&spec == m_cache_spectrum && spec.generation == m_cache_spec_gen && m_rbw == m_cache_rbw &&
        bin_width == m_cache_bin_width) {
        rbw_noise_W = m_cache_rbw_noise_W;
        rbw_tone_W = m_cache_rbw_tone_W;
    } else {
        std::vector<double> noise_W, tone_W;
        this->binPowerComponents(spec, noise_W, tone_W);
        rbw_noise_W = this->applyRBWForNoise(noise_W, bin_width);
        rbw_tone_W = this->applyRBW(tone_W, bin_width);
        m_cache_spectrum = &spec;
        m_cache_spec_gen = spec.generation;
        m_cache_rbw = m_rbw;
        m_cache_bin_width = bin_width;
        m_cache_rbw_noise_W = rbw_noise_W;
        m_cache_rbw_tone_W = rbw_tone_W;
    }

    std::vector<double> vbw_out = this->toDisplayPower_dBm(rbw_noise_W, rbw_tone_W, bin_width);
    return this->applyTraceMode(spec, vbw_out);
}

std::vector<double>
SpectrumAnalyzerEngine::renderCombinedSpectrum(const std::vector<const Spectrum *> &specs) const {
    // Every trace is resampled onto the common readout axis before summing, so bins from
    // different grids are never added by index (issue #293).
    const std::vector<double> grid = this->combinedReadoutAxis(specs);
    if (grid.empty()) {
        return {};
    }
    const double bin_width = gridBinWidth(grid);

    // Sum per-bin noise and tone power (W) separately so jitter can be applied
    // to the noise floor without disturbing tone peaks.
    std::vector<double> sum_noise_W(grid.size(), 0.0);
    std::vector<double> sum_tone_W(grid.size(), 0.0);
    for (const Spectrum *s : specs) {
        if (!s || s->frequencies.empty()) {
            continue;
        }
        std::vector<double> noise_W, tone_W;
        this->binPowerComponents(*s, noise_W, tone_W);
        accumulateOntoGrid(s->frequencies, noise_W, tone_W, grid, bin_width, sum_noise_W,
                           &sum_tone_W);
    }

    std::vector<double> rbw_noise_W = this->applyRBWForNoise(sum_noise_W, bin_width);
    std::vector<double> rbw_tone_W = this->applyRBW(sum_tone_W, bin_width);

    // Jitter and VBW act on the noise floor only; tone power is added back in linear watts.
    return this->toDisplayPower_dBm(rbw_noise_W, rbw_tone_W, bin_width);
}

double
SpectrumAnalyzerEngine::computeAverageNoiseLevel(const std::vector<const Spectrum *> &specs) const {
    const std::vector<double> grid = this->combinedReadoutAxis(specs);
    if (grid.empty()) {
        return -174.0;
    }
    const double bin_width = gridBinWidth(grid);

    // Sum noise power per bin (W) on the readout grid across all spectra, excluding tones
    std::vector<double> sum_noise_W(grid.size(), 0.0);
    for (const Spectrum *s : specs) {
        if (!s || s->frequencies.empty()) {
            continue;
        }
        std::vector<double> noise_W, tone_W;
        this->binPowerComponents(*s, noise_W, tone_W);
        accumulateOntoGrid(s->frequencies, noise_W, tone_W, grid, bin_width, sum_noise_W, nullptr);
    }

    std::vector<double> rbw_noise_W = this->applyRBWForNoise(sum_noise_W, bin_width);
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

std::optional<double>
SpectrumAnalyzerEngine::computeStrongestToneSNRdB(const Spectrum &spec) const {
    // Measurement only: render caches, jitter state, VBW and trace history stay
    // untouched, so a probe can ask for SNR between frames without perturbing them.

    // renderSpectrum() derives the bin width from the first two bins, so a grid
    // without two finite strictly ascending bins has nothing measurable on it.
    size_t n = spec.frequencies.size();
    if (n < 2) {
        return std::nullopt;
    }
    for (size_t i = 0; i < n; ++i) {
        if (!std::isfinite(spec.frequencies[i])) {
            return std::nullopt;
        }
        if (i > 0 && !(spec.frequencies[i] > spec.frequencies[i - 1])) {
            return std::nullopt;
        }
    }
    double bin_width = spec.frequencies[1] - spec.frequencies[0];

    // applyRBW() needs a positive, finite RBW: it divides by the bin width scaled
    // by m_rbw and would otherwise produce a degenerate kernel.
    if (!std::isfinite(m_rbw) || m_rbw <= 0.0) {
        return std::nullopt;
    }

    // Greatest finite stored tone power, in full (unsplit) linear watts.
    const Spectrum::Tone *strongest = nullptr;
    for (const auto &t : spec.tones) {
        if (!std::isfinite(t.freq_Hz) || !std::isfinite(t.power_dBm)) {
            continue;
        }
        if (strongest == nullptr || t.power_dBm > strongest->power_dBm) {
            strongest = &t;
        }
    }
    if (strongest == nullptr) {
        return std::nullopt;
    }

    // Nearest *actual* frequency entry. The grid is only guaranteed finite and
    // strictly ascending, so the first-bin spacing cannot be trusted to locate a
    // bin on its own; and a tone off either end of the swept span is unavailable
    // rather than rounding into an endpoint bin.
    if (strongest->freq_Hz < spec.frequencies.front() ||
        strongest->freq_Hz > spec.frequencies.back()) {
        return std::nullopt;
    }
    auto at_or_above =
        std::lower_bound(spec.frequencies.begin(), spec.frequencies.end(), strongest->freq_Hz);
    size_t bin_idx = static_cast<size_t>(at_or_above - spec.frequencies.begin());
    if (bin_idx > 0) {
        // A tone inside the span always has an entry at or above it, so bin_idx is
        // in range and only the lower neighbour needs comparing.
        double dist_above = spec.frequencies[bin_idx] - strongest->freq_Hz;
        double dist_below = strongest->freq_Hz - spec.frequencies[bin_idx - 1];
        if (dist_below <= dist_above) {
            --bin_idx; // Exact tie resolves to the lower index.
        }
    }

    // Noise exactly as renderSpectrum() builds it: density (W/Hz) times bin width,
    // then the noise-normalized RBW filter. Only the selected tone bin is read.
    std::vector<double> noise_W, tone_W;
    this->binPowerComponents(spec, noise_W, tone_W);
    std::vector<double> rbw_noise_W = this->applyRBWForNoise(noise_W, bin_width);
    if (rbw_noise_W.size() != n) {
        return std::nullopt;
    }
    double noise_linear_W = rbw_noise_W[static_cast<size_t>(bin_idx)];
    if (!std::isfinite(noise_linear_W) || noise_linear_W <= 0.0) {
        return std::nullopt;
    }

    // Full stored tone power: the real-domain display path halves it into a +-fc
    // pair, but SNR is quoted against the whole tone.
    // The tone is already in dBm. Convert only the positive noise power to dBm,
    // avoiding underflow or overflow if a finite tone level cannot be represented
    // as a double in watts.
    double noise_dBm = 10.0 * std::log10(noise_linear_W) + 30.0;
    double snr_dB = strongest->power_dBm - noise_dBm;
    if (!std::isfinite(snr_dB)) {
        return std::nullopt;
    }
    return snr_dB;
}

std::vector<double> SpectrumAnalyzerEngine::applyRBW(const std::vector<double> &power_W,
                                                     double binWidth) const {
    return applyRBWImpl(power_W, binWidth, false);
}

std::vector<double> SpectrumAnalyzerEngine::applyRBWForNoise(const std::vector<double> &power_W,
                                                             double binWidth) const {
    return applyRBWImpl(power_W, binWidth, true);
}

std::vector<double> SpectrumAnalyzerEngine::applyRBWImpl(const std::vector<double> &power_W,
                                                         double binWidth,
                                                         bool normalizeNoiseBandwidth) const {
    const std::ptrdiff_t n = static_cast<std::ptrdiff_t>(power_W.size());
    if (n == 0) {
        return {};
    }
    // A non-finite or non-positive RBW or bin width defines no filter: pass through.
    if (!std::isfinite(m_rbw) || m_rbw <= 0.0 || !std::isfinite(binWidth) || binWidth <= 0.0) {
        return power_W;
    }
    // Gaussian RBW filter. Its width follows RBW / binWidth. Noise-only convolution
    // normalizes its in-range discrete area at each output bin to the requested ENBW;
    // tone convolution keeps its center gain at one to preserve peak power.
    double rbw_bins = m_rbw / binWidth;
    constexpr double sqrt_2pi = 2.5066282746310002; // sqrt(2 * pi)
    double sigma = rbw_bins / sqrt_2pi;
    // Offsets past n-1 never reach a valid bin, so clamping the half-width to n leaves the
    // output unchanged. Clamp in floating point: an out-of-range double-to-int conversion is
    // undefined behavior.
    double half_bins = std::ceil(3.0 * sigma);
    half_bins = std::min(std::max(1.0, half_bins), static_cast<double>(n));
    const auto kernel_half = static_cast<std::ptrdiff_t>(half_bins);
    const std::ptrdiff_t kernel_size = 2 * kernel_half + 1;

    std::vector<double> kernel(static_cast<size_t>(kernel_size));
    for (std::ptrdiff_t i = 0; i < kernel_size; ++i) {
        // Offsets are computed in double: an int square overflows past |x| = 46340.
        const double x = static_cast<double>(i - kernel_half);
        // The peak is 1 by definition; it is also the 0/0 entry when sigma underflows to zero.
        kernel[static_cast<size_t>(i)] =
            (x == 0.0) ? 1.0 : std::exp(-0.5 * (x * x) / (sigma * sigma));
    }

    const bool normalize_noise = normalizeNoiseBandwidth && std::isfinite(rbw_bins);
    std::vector<double> out(static_cast<size_t>(n), 0.0);
    for (std::ptrdiff_t i = 0; i < n; ++i) {
        double acc = 0.0;
        double available_kernel_sum = 0.0;
        for (std::ptrdiff_t k = -kernel_half; k <= kernel_half; ++k) {
            const std::ptrdiff_t idx = i + k;
            if (idx < 0 || idx >= n) {
                continue;
            }
            const double weight = kernel[static_cast<size_t>(k + kernel_half)];
            acc += power_W[static_cast<size_t>(idx)] * weight;
            if (normalize_noise) {
                available_kernel_sum += weight;
            }
        }
        if (normalize_noise && available_kernel_sum > 0.0) {
            acc *= rbw_bins / available_kernel_sum;
        }
        out[static_cast<size_t>(i)] = acc;
    }
    return out;
}

std::vector<double> SpectrumAnalyzerEngine::applyVBW(const std::vector<double> &power_dBm,
                                                     double binWidth) const {
    const std::ptrdiff_t n = static_cast<std::ptrdiff_t>(power_dBm.size());

    if (n == 0) {
        return {};
    }

    // A non-finite or non-positive VBW or bin width defines no filter: pass through.
    if (!std::isfinite(m_vbw) || m_vbw <= 0.0 || !std::isfinite(binWidth) || binWidth <= 0.0) {
        return power_dBm;
    }

    // A window of 2n reaches every bin from every position, so clamping to it leaves the
    // output unchanged. Clamp in floating point: an out-of-range double-to-int conversion is
    // undefined behavior.
    double window_bins = std::round(m_vbw / binWidth);
    window_bins = std::min(std::max(1.0, window_bins), 2.0 * static_cast<double>(n));
    const auto window = static_cast<std::ptrdiff_t>(window_bins);
    std::vector<double> out(static_cast<size_t>(n), 0.0);

    const std::ptrdiff_t half = window / 2;
    for (std::ptrdiff_t i = 0; i < n; ++i) {
        double sum = 0.0;
        std::ptrdiff_t count = 0;
        for (std::ptrdiff_t k = -half; k <= half; ++k) {
            const std::ptrdiff_t idx = i + k;
            if (idx < 0 || idx >= n) {
                continue;
            }
            sum += power_dBm[static_cast<size_t>(idx)];
            ++count;
        }
        out[static_cast<size_t>(i)] =
            (count > 0) ? (sum / static_cast<double>(count)) : power_dBm[static_cast<size_t>(i)];
    }
    return out;
}

std::vector<double>
SpectrumAnalyzerEngine::applyTraceMode(const Spectrum &spec,
                                       const std::vector<double> &after_vbw) const {
    if (m_trace_mode == TraceMode::ClearWrite) {
        return after_vbw;
    }
    std::unordered_map<const Spectrum *, std::vector<double>> *history = nullptr;
    if (m_trace_mode == TraceMode::MaxHold)
        history = &m_max_hold;
    else if (m_trace_mode == TraceMode::MinHold)
        history = &m_min_hold;
    else if (m_trace_mode == TraceMode::VideoAverage)
        history = &m_video_avg;
    if (!history)
        return after_vbw;

    auto &h = (*history)[&spec];

    if (h.size() != after_vbw.size()) {
        h = after_vbw;
        return after_vbw;
    }

    if (m_trace_mode == TraceMode::MaxHold) {
        for (size_t i = 0; i < h.size(); ++i)
            h[i] = std::max(h[i], after_vbw[i]);
    } else if (m_trace_mode == TraceMode::MinHold) {
        for (size_t i = 0; i < h.size(); ++i)
            h[i] = std::min(h[i], after_vbw[i]);
    } else { // VideoAverage
        double alpha = 2.0 / (m_video_avg_count + 1);
        for (size_t i = 0; i < h.size(); ++i)
            h[i] = alpha * after_vbw[i] + (1.0 - alpha) * h[i];
    }
    return h;
}

void SpectrumAnalyzerEngine::setTraceMode(TraceMode m) {
    m_trace_mode = m;
    resetTraceHistory();
}

void SpectrumAnalyzerEngine::resetTraceHistory() const {
    m_max_hold.clear();
    m_min_hold.clear();
    m_video_avg.clear();
}

void SpectrumAnalyzerEngine::pruneHistory(const std::vector<const Spectrum *> &active_keys) const {
    std::unordered_set<const Spectrum *> active(active_keys.begin(), active_keys.end());
    for (auto *map : {&m_max_hold, &m_min_hold, &m_video_avg}) {
        for (auto it = map->begin(); it != map->end();) {
            if (active.find(it->first) == active.end()) {
                it = map->erase(it);
            } else {
                ++it;
            }
        }
    }
}
