#include "s_parameter_amplifier_engine.h"
#include "common.h"
#include "logging_core.h"
#include <algorithm>
#include <cmath>

namespace {
    inline double dbmToW(double dBm) { return std::pow(10.0, dBm / 10.0) * 0.001; }
    inline double wToDbm(double W) { return 10.0 * std::log10(W / 0.001); }
    inline double dbmToV(double dBm) { return std::sqrt(dbmToW(dBm) * R); }
    inline double vToDbm(double V) { return 10.0 * std::log10((V * V / R) / 0.001); }
}

SParameterAmplifierEngine::SParameterAmplifierEngine(int id, NodeGraphEngine& graph,
                                                       const std::string& filepath)
    : m_id(id), m_graph(&graph), m_filepath(filepath) {
    m_graph_node_id = graph.addNode("S-Param Amp " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
    reload(filepath);
}

void SParameterAmplifierEngine::recomputeCoefficients() {
    double V_oip2 = dbmToV(m_oip2_dBm);
    double V_oip3 = dbmToV(m_oip3_dBm);
    m_k1 = 1.0 / V_oip2;
    m_k2 = 4.0 / (3.0 * V_oip3 * V_oip3);
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
    if (m_enable_nonlinear && in_ptr && !in_ptr->tones.empty()) {
        size_t n_fund = out.tones.size();
        double total_distortion_mW = 0.0;

        // Harmonics
        for (const auto& tone : in_ptr->tones) {
            // Use S-param gain at the tone's frequency to find output power
            auto S_tone = m_data.interpolate(tone.freq_Hz, m_forward_param_idx);
            double tone_gain_dB = 20.0 * std::log10(std::abs(S_tone));
            double Pout_dBm = tone.power_dBm + tone_gain_dB;
            double Vp1 = dbmToV(Pout_dBm);

            double V_h2 = m_k1 * Vp1 / std::sqrt(2.0);
            double H2_dBm = vToDbm(V_h2);
            out.tones.push_back({tone.freq_Hz * 2.0, H2_dBm, 0.0});
            total_distortion_mW += dbmToW(H2_dBm);

            double V_h3 = m_k2 * Vp1 * Vp1 * Vp1 / 4.0;
            double H3_dBm = vToDbm(V_h3);
            out.tones.push_back({tone.freq_Hz * 3.0, H3_dBm, 0.0});
            total_distortion_mW += dbmToW(H3_dBm);
        }

        // IMD (cap at 3 tones)
        int n_tones = std::min(static_cast<int>(in_ptr->tones.size()), 3);
        for (int i = 0; i < n_tones; ++i) {
            for (int j = i + 1; j < n_tones; ++j) {
                auto S1 = m_data.interpolate(in_ptr->tones[i].freq_Hz, m_forward_param_idx);
                auto S2 = m_data.interpolate(in_ptr->tones[j].freq_Hz, m_forward_param_idx);
                double P1 = in_ptr->tones[i].power_dBm + 20.0 * std::log10(std::abs(S1));
                double P2 = in_ptr->tones[j].power_dBm + 20.0 * std::log10(std::abs(S2));
                double f1 = in_ptr->tones[i].freq_Hz;
                double f2 = in_ptr->tones[j].freq_Hz;
                double Vp1 = dbmToV(P1);
                double Vp2 = dbmToV(P2);

                double V_im2 = m_k1 * Vp1 * Vp2;
                double IM2_dBm = vToDbm(V_im2);
                out.tones.push_back({std::abs(f1 - f2), IM2_dBm, 0.0});
                out.tones.push_back({f1 + f2, IM2_dBm, 0.0});
                total_distortion_mW += 2.0 * dbmToW(IM2_dBm);

                double V_im3_12 = (3.0 / 4.0) * m_k2 * Vp1 * Vp1 * Vp2;
                double IM3_12_dBm = vToDbm(V_im3_12);
                out.tones.push_back({2.0 * f1 + f2, IM3_12_dBm, 0.0});
                out.tones.push_back({std::abs(2.0 * f1 - f2), IM3_12_dBm, 0.0});
                total_distortion_mW += 2.0 * dbmToW(IM3_12_dBm);

                double V_im3_21 = (3.0 / 4.0) * m_k2 * Vp1 * Vp2 * Vp2;
                double IM3_21_dBm = vToDbm(V_im3_21);
                out.tones.push_back({2.0 * f2 + f1, IM3_21_dBm, 0.0});
                out.tones.push_back({std::abs(f1 - 2.0 * f2), IM3_21_dBm, 0.0});
                total_distortion_mW += 2.0 * dbmToW(IM3_21_dBm);
            }
        }

        // Compression
        double Pfund_mW = 0.0;
        for (size_t k = 0; k < n_fund; ++k)
            Pfund_mW += dbmToW(out.tones[k].power_dBm);

        if (total_distortion_mW >= Pfund_mW || Pfund_mW <= 0.0) {
            for (size_t k = 0; k < n_fund; ++k)
                out.tones[k].power_dBm = MIN_POWER;
        } else {
            double ratio = 1.0 - total_distortion_mW / Pfund_mW;
            double ratio_dB = 10.0 * std::log10(ratio);
            for (size_t k = 0; k < n_fund; ++k)
                out.tones[k].power_dBm += ratio_dB;
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
    if (m_enable_nonlinear)
        s += " | OIP2: " + std::to_string(m_oip2_dBm) + " OIP3: " + std::to_string(m_oip3_dBm);
    return s;
}