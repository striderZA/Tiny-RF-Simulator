#include "combiner_engine.h"
#include "common.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <numbers>

CombinerEngine::CombinerEngine(int id, NodeGraphEngine &graph) : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("Combiner " + std::to_string(id), &m_node, 2, 1);
    m_node.inputs.resize(2);
    m_node.outputs.resize(1);
}

int CombinerEngine::inputPinId(int port) const {
    if (!m_graph || m_graph_node_id < 0 || port < 0 || port >= 2)
        return -1;
    for (const auto &node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            if (static_cast<size_t>(port) >= node.input_pin_ids.size())
                return -1;
            return node.input_pin_ids[port];
        }
    }
    return -1;
}

int CombinerEngine::outputPinId(int index) const {
    if (!m_graph || m_graph_node_id < 0 || index != 0)
        return -1;
    for (const auto &node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            if (node.output_pin_ids.empty())
                return -1;
            return node.output_pin_ids[0];
        }
    }
    return -1;
}

void CombinerEngine::setManualMode(bool enabled) {
    m_manual_mode = enabled;
    m_dirty = true;
}

void CombinerEngine::setSParamMode(bool enabled) {
    m_sparam_mode = enabled;
    m_dirty = true;
}

void CombinerEngine::setSParamFile(const std::string &path) {
    m_sparam_path = path;
    m_sparam_mode = m_sparam.load(path);
    m_dirty = true;
}

std::string CombinerEngine::hoverSummary() const { return "Combiner: 2→1, -3 dB"; }

nlohmann::json CombinerEngine::serialize() const {
    return {{"manual_mode", m_manual_mode},
            {"sparam_mode", m_sparam_mode},
            {"sparam_path", m_sparam_path}};
}

void CombinerEngine::deserialize(const nlohmann::json &j) {
    m_manual_mode = j.value("manual_mode", true);
    m_sparam_path = j.value("sparam_path", "");
    if (!m_sparam_path.empty())
        m_sparam.load(m_sparam_path);
    m_sparam_mode = j.value("sparam_mode", false) && m_sparam.loaded();
    m_dirty = true;
}

void CombinerEngine::update(double dt) {
    (void)dt;
    const Spectrum *in0 = m_node.inputs.size() > 0 ? m_node.inputs[0] : nullptr;
    const Spectrum *in1 = m_node.inputs.size() > 1 ? m_node.inputs[1] : nullptr;

    // --- S-parameter mode ---
    if (m_sparam_mode && m_sparam.loaded()) {
        if (!m_dirty && in0 == m_cached_input0_ptr && in1 == m_cached_input1_ptr &&
            (!in0 || in0->generation == m_cached_input0_generation) &&
            (!in1 || in1->generation == m_cached_input1_generation))
            return;

        m_dirty = false;
        m_cached_input0_ptr = in0;
        m_cached_input1_ptr = in1;
        if (in0)
            m_cached_input0_generation = in0->generation;
        if (in1)
            m_cached_input1_generation = in1->generation;

        auto &out = m_node.outputs[0];

        if (in0 && !in0->frequencies.empty()) {
            out.frequencies = in0->frequencies;
        } else if (in1 && !in1->frequencies.empty()) {
            out.frequencies = in1->frequencies;
        } else if (out.frequencies.size() < 2) {
            buildDefaultFrequencyGrid(out.frequencies);
        }

        const size_t N = out.frequencies.size();

        if (N < 2) {
            out.tones.clear();
            out.noise_W.assign(N, 0.0);
            out.noise_added_W.assign(N, 0.0);
            out.noise_total_W.assign(N, 0.0);
            out.phase_deg.assign(N, 0.0);
            out.bumpGeneration();
            return;
        }

        // 3-port device: port 0 = input 0, port 1 = input 1, port 2 = output
        // S21: input 0 -> output, S31: input 1 -> output
        int idx_S21 = 2 * m_sparam.numPorts() + 0;
        int idx_S31 = 2 * m_sparam.numPorts() + 1;

        std::vector<Spectrum::Tone> combined_tones;

        // Apply S21 to input 0 tones
        if (in0) {
            for (const auto &t : in0->tones) {
                auto S = m_sparam.interpolate(t.freq_Hz, idx_S21);
                Spectrum::Tone t_out = t;
                t_out.power_dBm += 20.0 * std::log10(std::abs(S));
                t_out.phase_deg += std::arg(S) * 180.0 / std::numbers::pi;
                combined_tones.push_back(t_out);
            }
        }

        // Apply S31 to input 1 tones
        if (in1) {
            for (const auto &t : in1->tones) {
                auto S = m_sparam.interpolate(t.freq_Hz, idx_S31);
                Spectrum::Tone t_out = t;
                t_out.power_dBm += 20.0 * std::log10(std::abs(S));
                t_out.phase_deg += std::arg(S) * 180.0 / std::numbers::pi;
                combined_tones.push_back(t_out);
            }
        }

        out.tones = combined_tones;
        out.is_complex_baseband =
            (in0 && in0->is_complex_baseband) || (in1 && in1->is_complex_baseband);

        // Noise: scale by |S21|^2 and |S31|^2 respectively
        const double k = 1.3806e-23;
        const double T = 290.0;

        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.resize(N);

        for (size_t i = 0; i < N; ++i) {
            auto S21 = m_sparam.interpolate(out.frequencies[i], idx_S21);
            auto S31 = m_sparam.interpolate(out.frequencies[i], idx_S31);
            double mag_sq_S21 = std::norm(S21);
            double mag_sq_S31 = std::norm(S31);

            double n0 = (in0 && i < in0->noise_total_W.size()) ? in0->noise_total_W[i] : 0.0;
            double n1 = (in1 && i < in1->noise_total_W.size()) ? in1->noise_total_W[i] : 0.0;

            out.noise_W[i] = n0 * mag_sq_S21 + n1 * mag_sq_S31;
            out.noise_added_W[i] = k * T * (1.0 - mag_sq_S21 - mag_sq_S31);
            out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
        }

        out.phase_deg.assign(N, 0.0);
        out.fs_Hz = in0 ? in0->fs_Hz : (in1 ? in1->fs_Hz : 0.0);
        out.bumpGeneration();
        return;
    }

    // --- Manual mode ---
    if (!m_dirty && in0 == m_cached_input0_ptr && in1 == m_cached_input1_ptr &&
        (!in0 || in0->generation == m_cached_input0_generation) &&
        (!in1 || in1->generation == m_cached_input1_generation))
        return;

    m_dirty = false;
    m_cached_input0_ptr = in0;
    m_cached_input1_ptr = in1;
    if (in0)
        m_cached_input0_generation = in0->generation;
    if (in1)
        m_cached_input1_generation = in1->generation;

    auto &out = m_node.outputs[0];

    if (in0 && !in0->frequencies.empty()) {
        out.frequencies = in0->frequencies;
    } else if (in1 && !in1->frequencies.empty()) {
        out.frequencies = in1->frequencies;
    } else if (out.frequencies.size() < 2) {
        buildDefaultFrequencyGrid(out.frequencies);
    }

    const size_t N = out.frequencies.size();

    if (N < 2) {
        out.tones.clear();
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
        out.phase_deg.assign(N, 0.0);
        out.bumpGeneration();
        return;
    }

    // Combine tones from both inputs with -3 dB loss
    double loss_linear = std::pow(10.0, -COMBINER_LOSS_DB / 10.0); // 0.5

    std::vector<Spectrum::Tone> combined_tones;

    // Add tones from input 0
    if (in0) {
        for (const auto &t : in0->tones) {
            Spectrum::Tone t_out = t;
            t_out.power_dBm -= COMBINER_LOSS_DB;
            combined_tones.push_back(t_out);
        }
    }

    // Add tones from input 1
    if (in1) {
        for (const auto &t : in1->tones) {
            Spectrum::Tone t_out = t;
            t_out.power_dBm -= COMBINER_LOSS_DB;
            combined_tones.push_back(t_out);
        }
    }

    out.tones = combined_tones;
    out.is_complex_baseband =
        (in0 && in0->is_complex_baseband) || (in1 && in1->is_complex_baseband);

    // Noise: incoherent power sum, then apply loss
    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double n0 = (in0 && i < in0->noise_total_W.size()) ? in0->noise_total_W[i] : 0.0;
        double n1 = (in1 && i < in1->noise_total_W.size()) ? in1->noise_total_W[i] : 0.0;
        out.noise_W[i] = loss_linear * (n0 + n1);
    }

    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W = out.noise_W;

    out.phase_deg.assign(N, 0.0);
    out.fs_Hz = in0 ? in0->fs_Hz : (in1 ? in1->fs_Hz : 0.0);
    out.bumpGeneration();
}
