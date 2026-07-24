#include "s_parameter_data.h"
#include "logging_core.h"
#include "touchstone_parser.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

bool SParameterData::load(const std::string &filepath) {
    m_freqs.clear();
    m_params.clear();
    m_num_ports = 0;

    auto data = TouchstoneParser::parse(filepath);
    if (!data.has_value()) {
        LOG_WARN("Failed to load S-parameter file: %s", filepath.c_str());
        return false;
    }
    m_num_ports = data->num_ports;
    m_freqs = std::move(data->frequencies);
    m_params = std::move(data->parameters);
    LOG_INFO("Loaded S-parameter data from %s (%zu points, %d ports)", filepath.c_str(),
             m_freqs.size(), m_num_ports);
    return true;
}

std::complex<double> SParameterData::interpolate(double freq_Hz, int param_idx) const {
    if (m_freqs.empty() || m_params.empty())
        return {1.0, 0.0};
    int total = m_num_ports * m_num_ports;
    if (param_idx < 0 || param_idx >= total)
        return {1.0, 0.0};
    if (freq_Hz <= m_freqs.front())
        return m_params.front()[param_idx];
    if (freq_Hz >= m_freqs.back())
        return m_params.back()[param_idx];

    auto it = std::lower_bound(m_freqs.begin(), m_freqs.end(), freq_Hz);
    size_t i =
        (it == m_freqs.begin()) ? 0 : static_cast<size_t>(std::distance(m_freqs.begin(), it) - 1);
    double f0 = m_freqs[i], f1 = m_freqs[i + 1];
    double t = (freq_Hz - f0) / (f1 - f0);
    auto p0 = m_params[i][param_idx];
    auto p1 = m_params[i + 1][param_idx];
    return p0 + t * (p1 - p0);
}

void SParameterData::applyToSpectrum(const Spectrum &in, Spectrum &out, int param_idx) const {
    if (!loaded()) {
        out.frequencies.clear();
        out.tones.clear();
        return;
    }

    if (!in.frequencies.empty()) {
        out.frequencies = in.frequencies;
    } else if (out.frequencies.size() < 2) {
        buildDefaultFrequencyGrid(out.frequencies);
    }

    const size_t N = out.frequencies.size();

    out.tones = in.tones;
    for (auto &t : out.tones) {
        auto S = interpolate(t.freq_Hz, param_idx);
        double mag = std::max(std::abs(S), std::numeric_limits<double>::min());
        t.power_dBm += 20.0 * std::log10(mag);
        t.phase_deg += std::arg(S) * 180.0 / std::numbers::pi;
    }

    if (!in.phase_deg.empty()) {
        out.phase_deg = in.phase_deg;
    } else {
        out.phase_deg.assign(N, 0.0);
    }

    if (N < 2) {
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
        return;
    }

    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        auto S = interpolate(out.frequencies[i], param_idx);
        double gain_linear = std::norm(S);
        double nin = (i < in.noise_total_W.size() ? in.noise_total_W[i] : 0.0);
        out.noise_W[i] = gain_linear * nin;
    }

    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i)
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
}
