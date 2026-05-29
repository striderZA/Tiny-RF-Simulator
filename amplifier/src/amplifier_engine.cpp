#include "amplifier_engine.h"
#include <cmath>

AmplifierEngine::AmplifierEngine(int id, NodeGraphEngine& graph)
    : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("Amplifier " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
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
    if (m_nonlinear.enabled() && in_ptr && !in_ptr->tones.empty()) {
        size_t n_fund = out.tones.size();
        double gain_linear = dbToLinear(m_gain_dB);
        auto result = m_nonlinear.process(in_ptr->tones,
            [gain_linear](double) { return gain_linear; });

        for (const auto& t : result.extra_tones)
            out.tones.push_back(t);

        if (result.compression_dB < -1e8) {
            for (size_t i = 0; i < n_fund; ++i)
                out.tones[i].power_dBm = MIN_POWER;
        } else {
            for (size_t i = 0; i < n_fund; ++i)
                out.tones[i].power_dBm += result.compression_dB;
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

void AmplifierEngine::serialize(nlohmann::json& j) const {
    j["gain_dB"] = m_gain_dB;
    j["nf_dB"] = m_nf_dB;
    j["enable_nonlinear"] = m_enable_nonlinear;
    j["oip2_dBm"] = m_oip2_dBm;
    j["oip3_dBm"] = m_oip3_dBm;
}

void AmplifierEngine::deserialize(const nlohmann::json& j) {
    m_gain_dB = j.value("gain_dB", 0.0);
    m_nf_dB = j.value("nf_dB", 0.0);
    m_enable_nonlinear = j.value("enable_nonlinear", false);
    m_oip2_dBm = j.value("oip2_dBm", 100.0);
    m_oip3_dBm = j.value("oip3_dBm", 100.0);
    if (m_enable_nonlinear) recomputeCoefficients();
    m_dirty = true;
}

std::string AmplifierEngine::hoverSummary() const {
    return "Gain: " + std::to_string(m_gain_dB) + " dB | NF: " + std::to_string(m_nf_dB) + " dB";
}
