#include "s_parameter_amplifier_engine.h"
#include "common.h"
#include "logging_core.h"
#include <algorithm>
#include <cmath>

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
    m_forward_param_idx = 0;

    if (!m_data.load(filepath))
        return;

    int np = m_data.numPorts();
    m_forward_param_idx = (np > 1) ? np : 0;
    m_dirty = true;
    LOG_INFO("Loaded S-parameter amplifier %d from %s (%zu points, %d ports)",
             m_id, filepath.c_str(), m_data.freqs().size(), np);
}

int SParameterAmplifierEngine::inputPinId() const {
    return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1;
}

int SParameterAmplifierEngine::outputPinId() const {
    return m_graph ? m_graph->outputPinId(m_graph_node_id) : -1;
}

void SParameterAmplifierEngine::setForwardParamIdx(int idx) {
    int total = m_data.paramCount();
    if (idx >= 0 && idx < total && idx != m_forward_param_idx) {
        m_forward_param_idx = idx;
        m_dirty = true;
    }
}

void SParameterAmplifierEngine::update(double dt) {
    (void)dt;
    const Spectrum* in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
    if (!m_dirty && in_ptr == m_cached_input_ptr && (!in_ptr || in_ptr->generation == m_cached_input_generation))
        return;
    m_dirty = false;
    m_cached_input_ptr = in_ptr;
    if (in_ptr)
        m_cached_input_generation = in_ptr->generation;

    Spectrum empty;
    const Spectrum& in = in_ptr ? *in_ptr : empty;
    auto& out = m_node.outputs[0];

    m_data.applyToSpectrum(in, out, m_forward_param_idx);

    if (!m_data.loaded() || out.frequencies.empty()) {
        out.bumpGeneration();
        return;
    }

    const size_t N = out.frequencies.size();
    if (N < 2) {
        out.bumpGeneration();
        return;
    }

    // Add noise figure
    double Te = calculateNoiseTemp(m_nf_dB);
    out.noise_added_W.resize(N);
    for (size_t i = 0; i < N; ++i) {
        auto S = m_data.interpolate(out.frequencies[i], m_forward_param_idx);
        double gain_linear = std::norm(S);
        out.noise_added_W[i] = k * Te * gain_linear;
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }

    // Nonlinear processing
    if (m_nonlinear.enabled() && in_ptr && !in_ptr->tones.empty()) {
        size_t n_fund = out.tones.size();
        auto result = m_nonlinear.process(in_ptr->tones,
            [this](double freq) {
                auto S = this->m_data.interpolate(freq, m_forward_param_idx);
                return std::abs(S);
            });

        for (const auto& t : result.extra_tones)
            out.tones.push_back(t);

        if (result.compression_dB < -1e8) {
            for (size_t k = 0; k < n_fund; ++k)
                out.tones[k].power_dBm = MIN_POWER;
        } else {
            for (size_t k = 0; k < n_fund; ++k)
                out.tones[k].power_dBm += result.compression_dB;
        }
    }

    out.bumpGeneration();
}

void SParameterAmplifierEngine::serialize(nlohmann::json& j) const {
    j["filepath"] = m_filepath;
    j["forward_param_idx"] = m_forward_param_idx;
    j["nf_dB"] = m_nf_dB;
    j["enable_nonlinear"] = m_enable_nonlinear;
    j["oip2_dBm"] = m_oip2_dBm;
    j["oip3_dBm"] = m_oip3_dBm;
}

void SParameterAmplifierEngine::deserialize(const nlohmann::json& j) {
    m_filepath = j.value("filepath", "");
    m_forward_param_idx = j.value("forward_param_idx", 0);
    m_nf_dB = j.value("nf_dB", 0.0);
    m_enable_nonlinear = j.value("enable_nonlinear", false);
    m_oip2_dBm = j.value("oip2_dBm", 100.0);
    m_oip3_dBm = j.value("oip3_dBm", 100.0);
    if (!m_filepath.empty())
        m_data.load(m_filepath);
    if (m_enable_nonlinear)
        recomputeCoefficients();
    m_dirty = true;
}

std::string SParameterAmplifierEngine::hoverSummary() const {
    if (!m_data.loaded()) return "Not loaded";
    int np = m_data.numPorts();
    std::string s = std::to_string(np) + "-port | Forward: S"
        + std::to_string((m_forward_param_idx / np) + 1)
        + std::to_string((m_forward_param_idx % np) + 1)
        + " | NF: " + std::to_string(m_nf_dB) + " dB";
    if (m_nonlinear.enabled())
        s += " | OIP2: " + std::to_string(m_nonlinear.oip2_dBm()) + " OIP3: " + std::to_string(m_nonlinear.oip3_dBm());
    return s;
}