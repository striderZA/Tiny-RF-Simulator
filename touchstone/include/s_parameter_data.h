#pragma once

#include "signal_node.h"
#include <complex>
#include <string>
#include <vector>

class SParameterData {
  public:
    bool load(const std::string &filepath);
    bool loaded() const { return !m_freqs.empty(); }

    int numPorts() const { return m_num_ports; }
    const std::vector<double> &freqs() const { return m_freqs; }
    const std::vector<std::vector<std::complex<double>>> &params() const { return m_params; }
    int paramCount() const { return m_num_ports * m_num_ports; }
    // True when freq_Hz lies inside the file's [first, last] band. Outside it, interpolate()
    // holds the nearest edge value; callers that need measured data MUST check this flag.
    bool inBand(double freq_Hz) const {
        return !m_freqs.empty() && freq_Hz >= m_freqs.front() && freq_Hz <= m_freqs.back();
    }

    std::complex<double> interpolate(double freq_Hz, int param_idx) const;
    void applyToSpectrum(const Spectrum &in, Spectrum &out, int param_idx) const;

  private:
    int m_num_ports = 0;
    std::vector<double> m_freqs;
    std::vector<std::vector<std::complex<double>>> m_params;
};