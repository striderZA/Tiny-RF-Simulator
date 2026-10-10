#include "network_analyzer_engine.h"

#include "common.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <unordered_map>

NetworkAnalyzerEngine::NetworkAnalyzerEngine(const NodeGraphEngine &graph,
                                             IMeasurementChainHost &host)
    : m_graph(graph), m_host(host) {}

void NetworkAnalyzerEngine::setStartFrequency(double hz) {
    if (hz != m_start_freq) {
        m_start_freq = hz;
        m_sweep_params_dirty = true;
    }
}

void NetworkAnalyzerEngine::setStopFrequency(double hz) {
    if (hz != m_stop_freq) {
        m_stop_freq = hz;
        m_sweep_params_dirty = true;
    }
}

void NetworkAnalyzerEngine::setPoints(int n) {
    int clamped = std::clamp(n, 2, 2001);
    if (clamped != m_points) {
        m_points = clamped;
        m_sweep_params_dirty = true;
    }
}

void NetworkAnalyzerEngine::setStimulusPower(double dBm) {
    if (dBm != m_stimulus_power_dBm) {
        m_stimulus_power_dBm = dBm;
        m_sweep_params_dirty = true;
    }
}

void NetworkAnalyzerEngine::setPointA(int pin_id) { m_point_a_pin = pin_id; }
void NetworkAnalyzerEngine::setPointB(int pin_id) { m_point_b_pin = pin_id; }

void NetworkAnalyzerEngine::rebuildStimulus() {
    const int n = m_points;
    m_stimulus_freqs.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double t = (n > 1) ? static_cast<double>(i) / (n - 1) : 0.0;
        m_stimulus_freqs[static_cast<size_t>(i)] = std::lerp(m_start_freq, m_stop_freq, t);
    }

    auto &out = m_stimulus;
    out.frequencies = m_stimulus_freqs;
    out.tones.resize(m_stimulus_freqs.size());
    for (size_t i = 0; i < m_stimulus_freqs.size(); ++i)
        out.tones[i] = {m_stimulus_freqs[i], m_stimulus_power_dBm, 0.0};

    out.noise_W.assign(m_stimulus_freqs.size(), k * T);
    out.noise_added_W.assign(m_stimulus_freqs.size(), 0.0);
    out.phase_deg.assign(m_stimulus_freqs.size(), 0.0);
    out.computeTotalNoise();
    out.fs_Hz = 0.0;
    out.is_complex_baseband = false;
    out.bumpGeneration();

    m_gain_dB.assign(m_stimulus_freqs.size(), std::numeric_limits<double>::quiet_NaN());
    m_nf_dB.assign(m_stimulus_freqs.size(), std::numeric_limits<double>::quiet_NaN());
}

void NetworkAnalyzerEngine::computeMeasurement() {
    const size_t N = m_stimulus_freqs.size();

    auto path = findMeasurementChainPath(m_graph, m_host, m_point_a_pin, m_point_b_pin);
    if (!path || path->components.size() < 2) {
        m_gain_dB.assign(N, std::numeric_limits<double>::quiet_NaN());
        m_nf_dB.assign(N, std::numeric_limits<double>::quiet_NaN());
        m_cached_signature.clear();
        return;
    }

    std::string signature = path->signature();
    char sweep_buf[192];
    std::snprintf(sweep_buf, sizeof(sweep_buf), "%.17g,%.17g,%d,%.17g,%d,%d", m_start_freq,
                  m_stop_freq, m_points, m_stimulus_power_dBm, m_point_a_pin, m_point_b_pin);
    signature += sweep_buf;
    if (signature == m_cached_signature)
        return;
    m_cached_signature = std::move(signature);

    m_gain_dB.assign(N, std::numeric_limits<double>::quiet_NaN());
    m_nf_dB.assign(N, std::numeric_limits<double>::quiet_NaN());
    IsolatedChainRunner runner(m_host);
    if (!runner.prepare(*path))
        return;
    const Spectrum *response = runner.run(m_stimulus);
    if (!response)
        return;
    constexpr double kFreqEpsilonHz = 1.0;
    const auto cell_of = [](double f) -> std::optional<long long> {
        const double cell = std::floor(f / kFreqEpsilonHz);
        const double min_cell = static_cast<double>(std::numeric_limits<long long>::min());
        const double max_cell = static_cast<double>(std::numeric_limits<long long>::max());
        // Reserve neighbor cells for the +/-1 Hz scan and avoid out-of-range FP-to-int casts.
        if (!std::isfinite(cell) || cell <= min_cell || cell >= max_cell)
            return std::nullopt;
        return static_cast<long long>(cell);
    };

    std::unordered_multimap<long long, size_t> tone_cells;
    tone_cells.reserve(response->tones.size());
    for (size_t t = 0; t < response->tones.size(); ++t) {
        if (const auto cell = cell_of(response->tones[t].freq_Hz))
            tone_cells.emplace(*cell, t);
    }
    std::unordered_multimap<long long, size_t> freq_cells;
    freq_cells.reserve(response->frequencies.size());
    for (size_t j = 0; j < response->frequencies.size(); ++j) {
        if (const auto cell = cell_of(response->frequencies[j]))
            freq_cells.emplace(*cell, j);
    }
    for (size_t i = 0; i < N; ++i) {
        const double f = m_stimulus_freqs[i];
        const auto cell = cell_of(f);
        if (!cell)
            continue;
        const long long c = *cell;
        // Gain reads the first matching tone at f (see the header): at a 2:1
        // switch that is the selected throw's tone when present, otherwise the
        // other throw's leakage if any, and never a sum of throws.
        std::optional<size_t> tone_idx;
        for (int offset = -1; offset <= 1; ++offset) {
            const long long cc = c + offset;
            auto range = tone_cells.equal_range(cc);
            for (auto it = range.first; it != range.second; ++it) {
                if (std::abs(response->tones[it->second].freq_Hz - f) <= kFreqEpsilonHz &&
                    (!tone_idx || it->second < *tone_idx))
                    tone_idx = it->second;
            }
        }
        if (!tone_idx)
            continue;

        const double gain_dB = response->tones[*tone_idx].power_dBm - m_stimulus_power_dBm;
        if (gain_dB < -100.0)
            continue; // indistinguishable from noise floor -> no data

        std::optional<size_t> noise_idx;
        for (int offset = -1; offset <= 1; ++offset) {
            const long long cc = c + offset;
            auto range = freq_cells.equal_range(cc);
            for (auto it = range.first; it != range.second; ++it) {
                if (std::abs(response->frequencies[it->second] - f) <= kFreqEpsilonHz &&
                    (!noise_idx || it->second < *noise_idx))
                    noise_idx = it->second;
            }
        }

        m_gain_dB[i] = gain_dB;

        if (noise_idx && *noise_idx < response->noise_total_W.size()) {
            const double gain_linear = dbToLinear(gain_dB);
            const double noise_out_W = response->noise_total_W[*noise_idx];
            const double nf_linear = (noise_out_W / gain_linear) / (k * T);
            if (nf_linear > 0.0)
                m_nf_dB[i] = 10.0 * std::log10(nf_linear);
        }
    }
}

void NetworkAnalyzerEngine::update() {
    if (m_sweep_params_dirty) {
        rebuildStimulus();
        m_sweep_params_dirty = false;
    }
    computeMeasurement();
}

nlohmann::json NetworkAnalyzerEngine::serialize() const {
    return {{"start_freq_hz", m_start_freq},
            {"stop_freq_hz", m_stop_freq},
            {"points", m_points},
            {"stimulus_power_dBm", m_stimulus_power_dBm},
            {"point_a_pin", m_point_a_pin},
            {"point_b_pin", m_point_b_pin}};
}

void NetworkAnalyzerEngine::deserialize(const nlohmann::json &j) {
    m_start_freq = j.value("start_freq_hz", 1e9);
    m_stop_freq = j.value("stop_freq_hz", 6e9);
    m_points = std::clamp(j.value("points", 201), 2, 2001);
    m_stimulus_power_dBm = j.value("stimulus_power_dBm", -30.0);
    m_point_a_pin = j.value("point_a_pin", -1);
    m_point_b_pin = j.value("point_b_pin", -1);
    m_sweep_params_dirty = true;
}
