#include "network_analyzer_engine.h"

#include "common.h"
#include "sweep_response.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>

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
    m_stimulus_freqs = linearSweepFrequencies(m_start_freq, m_stop_freq, m_points);
    m_stimulus = sweepStimulus(m_stimulus_freqs, m_stimulus_power_dBm);
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

    // Gain reads the first matching tone at f (see the header): at a 2:1 switch that is the
    // selected throw's tone when present, otherwise the other throw's leakage if any, and never a
    // sum of throws.
    const auto points = readSweepResponse(m_stimulus_freqs, *response);
    for (size_t i = 0; i < N; ++i) {
        const SweepPointResponse &point = points[i];
        // A tone an S-parameter block flagged out of band carries a held edge value, not a
        // measurement: leave the point NaN.
        if (!point.tone_dBm || point.tone_out_of_band)
            continue;
        const double gain_dB = *point.tone_dBm - m_stimulus_power_dBm;
        if (gain_dB < -100.0)
            continue; // indistinguishable from noise floor -> no data
        m_gain_dB[i] = gain_dB;

        // Likewise a bin flagged out of band leaves NF NaN.
        if (!point.noise_total_W || point.noise_out_of_band)
            continue;
        m_nf_dB[i] = sweepNoiseFigure_dB(*point.noise_total_W, gain_dB);
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
