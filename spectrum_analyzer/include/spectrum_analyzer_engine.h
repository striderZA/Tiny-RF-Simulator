#pragma once
#include "common.h"
#include "spectrum.h"
#include <random>
#include <vector>

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

    double startFrequency() const { return m_start_freq; }
    double stopFrequency() const { return m_stop_freq; }
    double minPower() const { return m_min_power; }
    double maxPower() const { return m_max_power; }
    double vbw() const { return m_vbw; }
    double rbw() const { return m_rbw; }

    double generate_noise_power();

    std::vector<double> renderSpectrum(const Spectrum &spec) const;

  private:
    // Config
    double m_start_freq = MIN_FREQ;
    double m_stop_freq = MAX_FREQ;
    double m_min_power = MIN_POWER;
    double m_max_power = MAX_POWER;
    double m_vbw = DEFAULT_VBW;
    double m_rbw = DEFAULT_RBW;

    std::vector<double> integratePowerPerBin(const Spectrum &spec) const;
    std::vector<double> applyRbw(const std::vector<double> &power_W, double bin_width) const;
    std::vector<double> applyVbw(const std::vector<double> &power_dBm, double bin_width) const;
};
