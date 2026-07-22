#pragma once
#include "common.h"
#include "spectrum.h"
#include <random>
#include <vector>
#include <unordered_map>

enum class TraceMode { ClearWrite, MaxHold, MinHold, VideoAverage };

class SpectrumAnalyzerEngine {
  public:
    SpectrumAnalyzerEngine();

    // Configuration API
    void setStartFrequency(double v) { m_start_freq = v; }
    void setStopFrequency(double v) { m_stop_freq = v; }
    void setMinPower(double v) { m_min_power = v; }
    void setMaxPower(double v) { m_max_power = v; }
    void setVideoBw(double v) { m_vbw = v; }
    void setResBw(double v) { m_rbw = v; }
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

    void setTraceMode(TraceMode m) { m_trace_mode = m; }
    void setVideoAvgCount(int n) { m_video_avg_count = n; }

    TraceMode traceMode() const { return m_trace_mode; }
    int videoAvgCount() const { return m_video_avg_count; }

    std::vector<double> renderSpectrum(const Spectrum &spec) const;
    std::vector<double> applyVBW(const std::vector<double> &power_dBm, double binWidth) const;
    std::vector<double> applyRBW(const std::vector<double> &power_W, double binWidth) const;
    std::vector<double> renderCombinedSpectrum(const std::vector<const Spectrum *> &specs) const;

    double computeAverageNoiseLevel(const std::vector<const Spectrum *> &specs) const;
    std::vector<Peak> findPeaks(const std::vector<double> &power_dBm,
                                const std::vector<double> &freq_axis,
                                size_t max_count = 8) const;

  private:
    // Config
    double m_start_freq = MIN_FREQ;
    double m_stop_freq = MAX_FREQ;
    double m_min_power = MIN_POWER;
    double m_max_power = MAX_POWER;
    double m_vbw = DEFAULT_VBW;
    double m_rbw = DEFAULT_RBW;

    std::vector<double> integratePowerPerBin(const Spectrum &spec) const;

    // Internal RBW cache: avoids re-applyRBW when spectrum data + settings haven't changed,
    // but still applies jitter + VBW every frame so the display looks alive.
    mutable const Spectrum* m_cache_spectrum = nullptr;
    mutable uint64_t m_cache_spec_gen = 0;
    mutable double m_cache_rbw = 0;
    mutable double m_cache_bin_width = 0;
    mutable std::vector<double> m_cache_rbw_power_W;

    mutable std::mt19937 m_rng;
    mutable bool m_noise_jitter_enabled = true;
    mutable double m_noise_jitter_sigma_dB = 1.5;

    mutable TraceMode m_trace_mode = TraceMode::ClearWrite;
    mutable int m_video_avg_count = 10;
};
