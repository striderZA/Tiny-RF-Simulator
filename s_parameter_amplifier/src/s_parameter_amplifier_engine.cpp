#include "s_parameter_amplifier_engine.h"
#include "common.h"
#include "touchstone_parser.h"
#include "logging_core.h"
#include <cmath>
#include <numbers>

SParameterAmplifierEngine::SParameterAmplifierEngine(int id, NodeGraphEngine& graph,
                                                       const std::string& filepath)
    : m_id(id), m_graph(&graph), m_filepath(filepath) {
    m_graph_node_id = graph.addNode("S-Param Amp " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
    reload(filepath);
}

void SParameterAmplifierEngine::reload(const std::string& filepath) {
    m_filepath = filepath;
    m_loaded = false;
    m_freqs.clear();
    m_params.clear();

    auto data = TouchstoneParser::parse(filepath);
    if (!data.has_value()) {
        LOG_WARN("Failed to load S-parameter file: %s", filepath.c_str());
        return;
    }

    m_num_ports = data->num_ports;
    m_freqs = data->frequencies;
    m_params = std::move(data->parameters);

    // Default forward param to S21 (index = num_ports in row-major order)
    m_forward_param_idx = (m_num_ports > 1) ? m_num_ports : 0;

    m_loaded = !m_freqs.empty();
    LOG_INFO("Loaded S-parameter amplifier %d from %s (%zu points, %d ports)",
             m_id, filepath.c_str(), m_freqs.size(), m_num_ports);
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

void SParameterAmplifierEngine::setForwardParamIdx(int idx) {
    int total = m_num_ports * m_num_ports;
    if (idx >= 0 && idx < total)
        m_forward_param_idx = idx;
}

std::complex<double> SParameterAmplifierEngine::interpolateParam(double freq_Hz,
                                                                   int param_idx) const {
    if (m_freqs.empty() || m_params.empty()) return {1.0, 0.0};
    if (param_idx < 0 || param_idx >= m_num_ports * m_num_ports) return {1.0, 0.0};
    if (freq_Hz <= m_freqs.front()) return m_params.front()[param_idx];
    if (freq_Hz >= m_freqs.back()) return m_params.back()[param_idx];

    size_t i = 0;
    while (i + 1 < m_freqs.size() && m_freqs[i + 1] < freq_Hz)
        ++i;
    double f0 = m_freqs[i], f1 = m_freqs[i + 1];
    double t = (freq_Hz - f0) / (f1 - f0);
    auto p0 = m_params[i][param_idx];
    auto p1 = m_params[i + 1][param_idx];
    return p0 + t * (p1 - p0);
}

void SParameterAmplifierEngine::update(double dt) {
    (void)dt;
    auto& in = m_node.inputs[0];
    auto& out = m_node.outputs[0];

    if (!m_loaded) {
        out.frequencies.clear();
        out.tones.clear();
        return;
    }

    if (!in.frequencies.empty()) {
        out.frequencies = in.frequencies;
    } else if (out.frequencies.size() < 2) {
        const double start_Hz = MIN_FREQ;
        const double stop_Hz = MAX_FREQ;
        const double f_step_Hz = 10e6;
        int n = static_cast<int>((stop_Hz - start_Hz) / f_step_Hz);
        if (n < 2) n = 2;
        out.frequencies.resize(n);
        for (int i = 0; i < n; ++i)
            out.frequencies[i] = start_Hz + i * f_step_Hz;
    }

    const size_t N = out.frequencies.size();

    out.tones = in.tones;
    for (auto& t : out.tones) {
        auto S = interpolateParam(t.freq_Hz, m_forward_param_idx);
        double gain_dB = 20.0 * std::log10(std::abs(S));
        t.power_dBm += gain_dB;
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
        auto S = interpolateParam(out.frequencies[i], m_forward_param_idx);
        double gain_linear = std::norm(S);
        double nin = (i < in.noise_total_W.size() ? in.noise_total_W[i] : 0.0);
        out.noise_W[i] = gain_linear * nin;
    }

    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i)
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
}
