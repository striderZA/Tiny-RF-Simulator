#pragma once
#include "common.h"
#include "spectrum.h"
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

enum class TraceMode { ClearWrite, MaxHold, MinHold, VideoAverage };

class SpectrumAnalyzerEngine {
  public:
    SpectrumAnalyzerEngine();

    // Configuration API
    void setStartFrequency(double v) { m_start_freq = v; }
    void setStopFrequency(double v) { m_stop_freq = v; }
    void setMinPower(double v) { m_min_power = v; }
    void setMaxPower(double v) { m_max_power = v; }
    void setVideoBw(double v) {
        if (m_vbw != v) {
            m_vbw = v;
            resetTraceHistory();
        }
    }
    void setResBw(double v) {
        if (m_rbw != v) {
            m_rbw = v;
            resetTraceHistory();
        }
    }
    void setNoiseJitterEnabled(bool v) { m_noise_jitter_enabled = v; }
    void setNoiseJitterSigmaDb(double v) { m_noise_jitter_sigma_dB = v; }

    bool noiseJitterEnabled() const { return m_noise_jitter_enabled; }
    double noiseJitterSigmaDb() const { return m_noise_jitter_sigma_dB; }

    double startFrequency() const { return m_start_freq; }
    double stopFrequency() const { return m_stop_freq; }
    double minPower() const { return m_min_power; }
    double maxPower() const { return m_max_power; }
    double vbw() const { return m_vbw; }
    double rbw() const { return m_rbw; }

    void setTraceMode(TraceMode m);
    void setVideoAvgCount(int n) {
        if (m_video_avg_count != n) {
            m_video_avg_count = n;
            resetTraceHistory();
        }
    }

    TraceMode traceMode() const { return m_trace_mode; }
    int videoAvgCount() const { return m_video_avg_count; }
    void resetTraceHistory() const;
    void pruneHistory(const std::vector<const Spectrum *> &active_keys) const;

    std::vector<double> renderSpectrum(const Spectrum &spec) const;
    // applyVBW() and applyRBW() return their input unchanged when the RBW or VBW, or the bin
    // width, is not finite and positive. A kernel half-width is clamped to the grid length and a
    // VBW window to twice it; the clamped offsets never reach a bin, so the output is unchanged.
    // The generic applyRBW() kernel remains unit-peak for tones; internal noise paths normalize
    // its sampled area to the requested RBW.
    std::vector<double> applyVBW(const std::vector<double> &power_dBm, double binWidth) const;
    std::vector<double> applyRBW(const std::vector<double> &power_W, double binWidth) const;
    // combinedReadoutAxis() is the common axis that renderCombinedSpectrum() and
    // computeAverageNoiseLevel() return values on: from the lowest start to the highest stop
    // of the traces, at the coarsest bin spacing among them. Each trace is sampled onto it
    // (noise density at the nearest source bin, scaled to the axis step; tone power to the
    // nearest axis bin), so traces on different grids are never added by index (issue #293).
    // Limits: a trace finer than the axis step contributes one sample per axis bin, so its
    // noise is an estimate; a single-bin trace puts its tone on the nearest axis bin.
    std::vector<double> combinedReadoutAxis(const std::vector<const Spectrum *> &specs) const;
    // Display trace: the combined spectrum with cosmetic noise jitter applied.
    std::vector<double> renderCombinedSpectrum(const std::vector<const Spectrum *> &specs) const;
    // Same readout-axis and RBW/VBW chain without jitter. Peak and marker readouts use this
    // trace so their values are not biased or frame-variant (issue #292).
    std::vector<double>
    renderCombinedReadoutSpectrum(const std::vector<const Spectrum *> &specs) const;

    double computeAverageNoiseLevel(const std::vector<const Spectrum *> &specs) const;
    std::vector<Peak> findPeaks(const std::vector<double> &power_dBm,
                                const std::vector<double> &freq_axis, size_t max_count = 8) const;

    // Strongest-tone SNR of one spectrum, in dB: the greatest finite stored tone
    // power against the RBW-filtered noise power at the grid entry nearest that
    // tone's frequency (ties resolve to the lower index).
    // Both sides are full linear watts, so a real-domain tone is measured at its
    // stored power rather than at the 3 dB-low +-fc display peak. Returns nullopt
    // when there is no finite tone, no two-bin finite strictly ascending grid, a
    // tone outside the swept span, a non-finite/non-positive RBW, no positive
    // filtered noise under it, or a non-finite dB result. Computed as a difference
    // of logarithms, so even an extreme (but finite) signal-to-noise ratio returns
    // its true dB value instead of overflowing the linear ratio. Read-only: it
    // never touches the render caches, jitter state, VBW, or trace history.
    std::optional<double> computeStrongestToneSNRdB(const Spectrum &spec) const;

  private:
    // Config
    double m_start_freq = MIN_FREQ;
    double m_stop_freq = MAX_FREQ;
    double m_min_power = MIN_POWER;
    double m_max_power = MAX_POWER;
    double m_vbw = DEFAULT_VBW;
    double m_rbw = DEFAULT_RBW;

    // Splits a spectrum into per-bin noise power (W) and per-bin tone impulse
    // power (W) on its own frequency grid. Both are linear in power, so callers
    // may filter or jitter them independently before recombining.
    void binPowerComponents(const Spectrum &spec, std::vector<double> &noise_W,
                            std::vector<double> &tone_W) const;
    // Jittered noise floor (dBm per bin) from RBW-filtered noise power (W). When apply_jitter
    // is set, the cosmetic jitter scales only this component; a non-positive sigma or a
    // disabled jitter setting leaves it unjittered.
    std::vector<double> toJitteredNoise_dBm(const std::vector<double> &noise_W,
                                            bool apply_jitter) const;
    // Builds the dBm display trace: VBW smooths the noise floor only, then the RBW-filtered
    // tone power is added in linear watts, so tone peaks are never averaged.
    std::vector<double> toDisplayPower_dBm(const std::vector<double> &noise_W,
                                           const std::vector<double> &tone_W, double binWidth,
                                           bool apply_jitter) const;
    std::vector<double> renderCombinedTrace(const std::vector<const Spectrum *> &specs,
                                            bool apply_jitter) const;
    std::vector<double> applyRBWForNoise(const std::vector<double> &power_W, double binWidth) const;
    std::vector<double> applyRBWImpl(const std::vector<double> &power_W, double binWidth,
                                     bool normalizeNoiseBandwidth) const;

    std::vector<double> applyTraceMode(const Spectrum &spec,
                                       const std::vector<double> &after_vbw) const;

    // Internal RBW cache: avoids re-applyRBW when spectrum data + settings haven't changed,
    // but still applies jitter + VBW every frame so the display looks alive.
    mutable const Spectrum *m_cache_spectrum = nullptr;
    mutable uint64_t m_cache_spec_gen = 0;
    mutable double m_cache_rbw = 0;
    mutable double m_cache_bin_width = 0;
    mutable std::vector<double> m_cache_rbw_noise_W;
    mutable std::vector<double> m_cache_rbw_tone_W;

    mutable std::mt19937 m_rng;
    mutable bool m_noise_jitter_enabled = true;
    mutable double m_noise_jitter_sigma_dB = 1.5;

    mutable TraceMode m_trace_mode = TraceMode::ClearWrite;
    mutable std::unordered_map<const Spectrum *, std::vector<double>> m_max_hold;
    mutable std::unordered_map<const Spectrum *, std::vector<double>> m_min_hold;
    mutable std::unordered_map<const Spectrum *, std::vector<double>> m_video_avg;

    mutable int m_video_avg_count = 10;
};
