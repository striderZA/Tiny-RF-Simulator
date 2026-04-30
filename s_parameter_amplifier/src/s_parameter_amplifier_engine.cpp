#include "s_parameter_amplifier_engine.h"
#include "logging_core.h"
#include <cmath>

SParameterAmplifierEngine::SParameterAmplifierEngine(int id, NodeGraphEngine& graph,
                                                      const std::string& filepath)
    : m_id(id), m_graph(&graph), m_filepath(filepath) {
    m_graph_node_id = graph.addNode("S-Param Amp " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);

    auto data = TouchstoneParser::parse(filepath);
    if (!data.has_value()) {
        LOG_WARN("Failed to load S-parameter file: %s", filepath.c_str());
        return;
    }

    if (data->num_ports != 2) {
        LOG_WARN("S-parameter file must be 2-port, got %d ports: %s", data->num_ports, filepath.c_str());
        return;
    }

    m_s21_freqs.reserve(data->frequencies.size());
    m_s21_mag.reserve(data->frequencies.size());

    for (size_t i = 0; i < data->frequencies.size(); ++i) {
        m_s21_freqs.push_back(data->frequencies[i]);
        // S21 is the 2nd parameter in the 2-port v1.0 order (N11, N21, N12, N22)
        const auto& s21 = data->parameters[i][1];
        m_s21_mag.push_back(std::abs(s21));
    }

    m_loaded = true;
    LOG_INFO("Loaded S-parameter amplifier %d from %s (%zu points)", id, filepath.c_str(),
             m_s21_freqs.size());
}

int SParameterAmplifierEngine::inputPinId() const {
    if (!m_graph || m_graph_node_id < 0) return -1;
    for (const auto& node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            return node.input_pin_ids.empty() ? -1 : node.input_pin_ids[0];
        }
    }
    return -1;
}

int SParameterAmplifierEngine::outputPinId() const {
    if (!m_graph || m_graph_node_id < 0) return -1;
    for (const auto& node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            return node.output_pin_ids.empty() ? -1 : node.output_pin_ids[0];
        }
    }
    return -1;
}

double SParameterAmplifierEngine::interpolateS21Mag(double freq_Hz) const {
    if (m_s21_freqs.empty()) return 1.0;
    if (freq_Hz <= m_s21_freqs.front()) return m_s21_mag.front();
    if (freq_Hz >= m_s21_freqs.back()) return m_s21_mag.back();

    // Linear interpolation
    size_t i = 0;
    while (i + 1 < m_s21_freqs.size() && m_s21_freqs[i + 1] < freq_Hz) {
        ++i;
    }
    double f0 = m_s21_freqs[i];
    double f1 = m_s21_freqs[i + 1];
    double m0 = m_s21_mag[i];
    double m1 = m_s21_mag[i + 1];
    double t = (freq_Hz - f0) / (f1 - f0);
    return m0 + t * (m1 - m0);
}

void SParameterAmplifierEngine::update(double dt) {
    (void)dt;
    auto& in = m_node.inputs[0];
    auto& out = m_node.outputs[0];

    if (!in.frequencies.empty()) {
        out.frequencies = in.frequencies;
    } else if (out.frequencies.size() < 2) {
        const double start_Hz = MIN_FREQ;
        const double stop_Hz = MAX_FREQ;
        const double f_step_Hz = 10e6;
        int n = static_cast<int>((stop_Hz - start_Hz) / f_step_Hz);
        if (n < 2) n = 2;
        out.frequencies.resize(n);
        for (int i = 0; i < n; ++i) {
            out.frequencies[i] = start_Hz + i * f_step_Hz;
        }
    }

    const size_t N = out.frequencies.size();

    // Copy tones and apply frequency-dependent gain
    out.tones = in.tones;
    for (auto& t : out.tones) {
        double mag = interpolateS21Mag(t.freq_Hz);
        double gain_dB = 20.0 * std::log10(mag);
        t.power_dBm += gain_dB;
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
        out.phase_deg.assign(N, 0.0);
        return;
    }

    // Noise: scale by |S21|² at each frequency bin
    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double mag = interpolateS21Mag(out.frequencies[i]);
        double gain_linear = mag * mag;
        double nin = (i < in.noise_total_W.size() ? in.noise_total_W[i] : 0.0);
        out.noise_W[i] = gain_linear * nin;
    }

    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i) {
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }
}
