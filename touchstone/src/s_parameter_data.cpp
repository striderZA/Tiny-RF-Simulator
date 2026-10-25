#include "s_parameter_data.h"
#include "logging_core.h"
#include "touchstone_parser.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace {

constexpr double kSystemImpedance = 50.0; // Ohms, the simulator's reference

// Solves M X = R in place for X (both n x n, row-major). M is destroyed and R
// holds X on return. Returns false when M is singular.
bool solveInPlace(std::vector<std::complex<double>> &m, std::vector<std::complex<double>> &r,
                  int n) {
    for (int c = 0; c < n; ++c) {
        int pivot = c;
        for (int row = c + 1; row < n; ++row)
            if (std::abs(m[row * n + c]) > std::abs(m[pivot * n + c]))
                pivot = row;
        if (!(std::abs(m[pivot * n + c]) > 0.0))
            return false;
        if (pivot != c) {
            for (int j = 0; j < n; ++j) {
                std::swap(m[pivot * n + j], m[c * n + j]);
                std::swap(r[pivot * n + j], r[c * n + j]);
            }
        }
        for (int row = 0; row < n; ++row) {
            if (row == c)
                continue;
            std::complex<double> f = m[row * n + c] / m[c * n + c];
            if (f == std::complex<double>(0.0, 0.0))
                continue;
            for (int j = c; j < n; ++j)
                m[row * n + j] -= f * m[c * n + j];
            for (int j = 0; j < n; ++j)
                r[row * n + j] -= f * r[c * n + j];
        }
    }
    for (int row = 0; row < n; ++row) {
        std::complex<double> d = m[row * n + row];
        for (int j = 0; j < n; ++j)
            r[row * n + j] /= d;
    }
    return true;
}

// Renormalises one n-port S matrix (row-major) from a uniform reference R to 50 ohm.
// Z = R (I-S)^-1 (I+S) and S' = (Z-50)(Z+50)^-1. Both factors are polynomials in S,
// so they commute and S' = A B^-1 with A = (R-50) I + (R+50) S and
// B = (R+50) I + (R-50) S. This needs no inverse of (I-S), so S = 1 (open) is fine.
bool renormalizeTo50(std::vector<std::complex<double>> &s, int n, double ref) {
    if (!(ref > 0.0) || !std::isfinite(ref))
        return false;
    using cd = std::complex<double>;
    std::vector<cd> a(s.size()), b(s.size());
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            cd sij = s[i * n + j];
            cd eye = (i == j) ? cd(1.0, 0.0) : cd(0.0, 0.0);
            a[i * n + j] = (ref - kSystemImpedance) * eye + (ref + kSystemImpedance) * sij;
            b[i * n + j] = (ref + kSystemImpedance) * eye + (ref - kSystemImpedance) * sij;
        }
    }
    if (!solveInPlace(b, a, n))
        return false;
    s = std::move(a);
    return true;
}

} // namespace

bool SParameterData::load(const std::string &filepath) {
    m_freqs.clear();
    m_params.clear();
    m_num_ports = 0;

    auto data = TouchstoneParser::parse(filepath);
    if (!data.has_value()) {
        LOG_WARN("Failed to load S-parameter file: %s", filepath.c_str());
        return false;
    }
    if (data->parameter != TouchstoneData::Parameter::S) {
        LOG_WARN("Touchstone file does not contain S-parameters: %s", filepath.c_str());
        return false;
    }
    if (data->reference_impedance != kSystemImpedance) {
        for (auto &point : data->parameters) {
            if (!renormalizeTo50(point, data->num_ports, data->reference_impedance)) {
                LOG_WARN("Cannot renormalise S-parameters from %g ohm to 50 ohm: %s",
                         data->reference_impedance, filepath.c_str());
                return false;
            }
        }
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
    const bool negative_frequency = freq_Hz < 0.0;
    freq_Hz = std::abs(freq_Hz);
    if (freq_Hz <= m_freqs.front()) {
        auto response = m_params.front()[param_idx];
        return negative_frequency ? std::conj(response) : response;
    }
    if (freq_Hz >= m_freqs.back()) {
        auto response = m_params.back()[param_idx];
        return negative_frequency ? std::conj(response) : response;
    }

    auto it = std::lower_bound(m_freqs.begin(), m_freqs.end(), freq_Hz);
    size_t i =
        (it == m_freqs.begin()) ? 0 : static_cast<size_t>(std::distance(m_freqs.begin(), it) - 1);
    double f0 = m_freqs[i], f1 = m_freqs[i + 1];
    double t = (freq_Hz - f0) / (f1 - f0);
    auto p0 = m_params[i][param_idx];
    auto p1 = m_params[i + 1][param_idx];
    auto response = p0 + t * (p1 - p0);
    return negative_frequency ? std::conj(response) : response;
}

void SParameterData::markOutOfBand(const Spectrum *in, Spectrum &out) const {
    const size_t N = out.frequencies.size();
    std::vector<bool> flags(N, false);
    if (in && in->out_of_band.size() == N)
        flags = in->out_of_band;
    for (size_t i = 0; i < N; ++i)
        flags[i] = flags[i] || !inBand(out.frequencies[i]);
    for (auto &t : out.tones)
        t.out_of_band = t.out_of_band || !inBand(t.freq_Hz);
    out.out_of_band = std::move(flags);
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
    out.is_complex_baseband = in.is_complex_baseband;
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
