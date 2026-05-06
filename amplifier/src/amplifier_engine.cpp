#include "amplifier_engine.h"
#include <cmath>

namespace {
    inline double dbmToW(double dBm) { return std::pow(10.0, dBm / 10.0) * 0.001; }
    inline double wToDbm(double W) { return 10.0 * std::log10(W / 0.001); }
    inline double dbmToV(double dBm) { return std::sqrt(dbmToW(dBm) * R); }
    inline double vToDbm(double V) { return 10.0 * std::log10((V * V / R) / 0.001); }
}

AmplifierEngine::AmplifierEngine(int id, NodeGraphEngine& graph)
    : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("Amplifier " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
}

void AmplifierEngine::recomputeCoefficients() {
    double V_oip2 = dbmToV(m_oip2_dBm);
    double V_oip3 = dbmToV(m_oip3_dBm);
    m_k1 = 1.0 / V_oip2;
    m_k2 = 4.0 / (3.0 * V_oip3 * V_oip3);
}

int AmplifierEngine::inputPinId() const {
    return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1;
}

int AmplifierEngine::outputPinId() const {
    return m_graph ? m_graph->outputPinId(m_graph_node_id) : -1;
}

void AmplifierEngine::update(double dt) {
    (void)dt;
    const Spectrum* in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
    if (!m_dirty && in_ptr == m_cached_input_ptr && (!in_ptr || in_ptr->generation == m_cached_input_generation))
        return;
    m_dirty = false;
    m_cached_input_ptr = in_ptr;
    if (in_ptr)
        m_cached_input_generation = in_ptr->generation;

    auto &out = m_node.outputs[0];

    if (in_ptr && !in_ptr->frequencies.empty()) {
        out.frequencies = in_ptr->frequencies;
    } else if (out.frequencies.size() < 2) {
        buildDefaultFrequencyGrid(out.frequencies);
    }

    out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};
    for (auto &t : out.tones) {
        t.power_dBm += m_gain_dB;
    }

    // Nonlinear processing
    if (m_enable_nonlinear && in_ptr && !in_ptr->tones.empty()) {
        size_t n_fund = out.tones.size();
        double total_distortion_mW = 0.0;

        // --- Harmonics (from each input tone) ---
        for (const auto &tone : in_ptr->tones) {
            double Pout_dBm = tone.power_dBm + m_gain_dB;
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

        // --- IMD (from unique tone pairs, cap at 3 tones) ---
        int n_tones = std::min(static_cast<int>(in_ptr->tones.size()), 3);
        for (int i = 0; i < n_tones; ++i) {
            for (int j = i + 1; j < n_tones; ++j) {
                double P1 = in_ptr->tones[i].power_dBm + m_gain_dB;
                double P2 = in_ptr->tones[j].power_dBm + m_gain_dB;
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

        // --- Compression ---
        double Pfund_mW = 0.0;
        for (size_t i = 0; i < n_fund; ++i) {
            Pfund_mW += dbmToW(out.tones[i].power_dBm);
        }

        if (total_distortion_mW >= Pfund_mW || Pfund_mW <= 0.0) {
            for (size_t i = 0; i < n_fund; ++i)
                out.tones[i].power_dBm = MIN_POWER;
        } else {
            double ratio = 1.0 - total_distortion_mW / Pfund_mW;
            double ratio_dB = 10.0 * std::log10(ratio);
            for (size_t i = 0; i < n_fund; ++i)
                out.tones[i].power_dBm += ratio_dB;
        }
    }

    const size_t N = out.frequencies.size();

    if (in_ptr && !in_ptr->phase_deg.empty()) {
        out.phase_deg = in_ptr->phase_deg;
    } else {
        out.phase_deg.assign(N, 0.0);
    }
    if (N < 2) {
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
        out.phase_deg.assign(N, 0.0);
        out.bumpGeneration();
        return;
    }

    double G = dbToLinear(m_gain_dB);
    double added_density = addedNoiseDensity_W_per_Hz(m_nf_dB, G);

    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double nin = (in_ptr && i < in_ptr->noise_total_W.size() ? in_ptr->noise_total_W[i] : 0.0);
        out.noise_W[i] = G * nin;
    }

    out.noise_added_W.resize(N);
    if (added_density <= 0.0) {
        out.noise_added_W.assign(N, 0.0);
    } else {
        out.noise_added_W.assign(N, added_density);
    }
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i) {
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }

    out.bumpGeneration();
}

std::string AmplifierEngine::hoverSummary() const {
    return "Gain: " + std::to_string(m_gain_dB) + " dB | NF: " + std::to_string(m_nf_dB) + " dB";
}
