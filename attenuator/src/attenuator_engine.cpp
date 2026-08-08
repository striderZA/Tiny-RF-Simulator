#include "attenuator_engine.h"
#include "common.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <numbers>

AttenuatorEngine::AttenuatorEngine(int id, NodeGraphEngine &graph) : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("Attenuator " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
}

int AttenuatorEngine::inputPinId() const {
    return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1;
}

int AttenuatorEngine::outputPinId(int index) const {
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

void AttenuatorEngine::setAttenuation(double dB) {
    m_atten_dB = std::clamp(dB, 0.0, 200.0);
    m_dirty = true;
}

void AttenuatorEngine::setSParamMode(bool enabled) {
    m_sparam_mode = enabled;
    m_dirty = true;
}

void AttenuatorEngine::setSParamFile(const std::string &path) {
    m_sparam_path = path;
    m_sparam_mode = m_sparam.load(path);
    m_dirty = true;
}

void AttenuatorEngine::update(double dt) {
    (void)dt;
    const Spectrum *in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];

    // --- S-parameter mode ---
    if (m_sparam_mode && m_sparam.loaded()) {
        if (!m_dirty && in_ptr == m_cached_input_ptr &&
            (!in_ptr || in_ptr->generation == m_cached_input_generation))
            return;
        m_dirty = false;
        m_cached_input_ptr = in_ptr;
        if (in_ptr)
            m_cached_input_generation = in_ptr->generation;

        auto &out = m_node.outputs[0];

        if (in_ptr && !in_ptr->frequencies.empty())
            out.frequencies = in_ptr->frequencies;
        else if (out.frequencies.size() < 2)
            buildDefaultFrequencyGrid(out.frequencies);

        const size_t N = out.frequencies.size();
        int idx = 1 * m_sparam.numPorts() + 0; // S21 index

        out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};
        out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
        for (auto &t : out.tones) {
            auto S = m_sparam.interpolate(t.freq_Hz, idx);
            t.power_dBm += 20.0 * std::log10(std::abs(S));
            t.phase_deg += std::arg(S) * 180.0 / std::numbers::pi;
        }

        if (in_ptr && !in_ptr->phase_deg.empty())
            out.phase_deg = in_ptr->phase_deg;
        else
            out.phase_deg.assign(N, 0.0);

        if (N < 2) {
            out.noise_W.assign(N, 0.0);
            out.noise_added_W.assign(N, 0.0);
            out.noise_total_W.assign(N, 0.0);
            out.bumpGeneration();
            return;
        }

        // Passive noise model: noise_total = noise_in * |S21|^2 + k*T*(1 - |S21|^2)
        const double k = 1.3806e-23;
        const double T = 290.0;

        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.resize(N);

        for (size_t i = 0; i < N; ++i) {
            auto S = m_sparam.interpolate(out.frequencies[i], idx);
            double mag_sq = std::norm(S); // |S21|^2
            double noise_in =
                (in_ptr && i < in_ptr->noise_total_W.size() ? in_ptr->noise_total_W[i] : 0.0);

            out.noise_W[i] = noise_in * mag_sq;
            out.noise_added_W[i] = k * T * (1.0 - mag_sq);
            out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
        }

        out.fs_Hz = in_ptr ? in_ptr->fs_Hz : 0.0;
        out.bumpGeneration();
        return;
    }

    // --- Manual mode ---
    if (!m_dirty && in_ptr == m_cached_input_ptr &&
        (!in_ptr || in_ptr->generation == m_cached_input_generation))
        return;
    m_dirty = false;
    m_cached_input_ptr = in_ptr;
    if (in_ptr)
        m_cached_input_generation = in_ptr->generation;

    auto &out = m_node.outputs[0];

    if (in_ptr && !in_ptr->frequencies.empty())
        out.frequencies = in_ptr->frequencies;
    else if (out.frequencies.size() < 2)
        buildDefaultFrequencyGrid(out.frequencies);

    const size_t N = out.frequencies.size();

    // Attenuate tones
    out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};
    out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
    for (auto &t : out.tones) {
        t.power_dBm -= m_atten_dB;
        // Phase unchanged in manual mode
    }

    // Copy phase
    if (in_ptr && !in_ptr->phase_deg.empty())
        out.phase_deg = in_ptr->phase_deg;
    else
        out.phase_deg.assign(N, 0.0);

    if (N < 2) {
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
        out.bumpGeneration();
        return;
    }

    // Passive noise model: noise_total = noise_in * G + k*T*(1 - G)
    const double k = 1.3806e-23;
    const double T = 290.0;
    const double G_linear = std::pow(10.0, -m_atten_dB / 10.0);

    out.noise_W.assign(N, 0.0);
    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.resize(N);

    for (size_t i = 0; i < N; ++i) {
        double noise_in =
            (in_ptr && i < in_ptr->noise_total_W.size() ? in_ptr->noise_total_W[i] : 0.0);

        out.noise_W[i] = noise_in * G_linear;
        out.noise_added_W[i] = k * T * (1.0 - G_linear);
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }

    out.fs_Hz = in_ptr ? in_ptr->fs_Hz : 0.0;
    out.bumpGeneration();
}

nlohmann::json AttenuatorEngine::serialize() const {
    return {
        {"atten_dB", m_atten_dB}, {"sparam_mode", m_sparam_mode}, {"sparam_path", m_sparam_path}};
}

void AttenuatorEngine::deserialize(const nlohmann::json &j) {
    m_atten_dB =
        j.contains("atten_dB") ? j["atten_dB"].get<double>() : j.value("attenuation_dB", 0.0);
    m_sparam_path = j.value("sparam_path", "");
    if (!m_sparam_path.empty())
        m_sparam.load(m_sparam_path);
    m_sparam_mode = j.value("sparam_mode", false) && m_sparam.loaded();
    m_dirty = true;
}

std::string AttenuatorEngine::hoverSummary() const {
    return "Attenuator: -" + std::to_string(static_cast<int>(m_atten_dB)) + " dB";
}
