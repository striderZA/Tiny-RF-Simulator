#include "amplifier_engine.h"
#include <cmath>
#include <nlohmann/json.hpp>
#include <numbers>

AmplifierEngine::AmplifierEngine(int id, NodeGraphEngine &graph) : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("Amplifier " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
}

void AmplifierEngine::setSParamFilepath(const std::string &path) {
    m_sparam_filepath = path;
    m_sparam_mode = m_sparam_data.load(path);
    if (m_sparam_data.loaded())
        m_sparam_fwd_idx = 1 * m_sparam_data.numPorts() + 0; // S21
    m_dirty = true;
}

int AmplifierEngine::inputPinId() const {
    return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1;
}

int AmplifierEngine::outputPinId() const {
    return m_graph ? m_graph->outputPinId(m_graph_node_id) : -1;
}

void AmplifierEngine::update(double dt) {
    (void)dt;
    const Spectrum *in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
    if (!m_dirty && in_ptr == m_cached_input_ptr &&
        (!in_ptr || in_ptr->generation == m_cached_input_generation))
        return;

    // --- S-parameter mode ---
    if (m_sparam_mode && m_sparam_data.loaded()) {
        if (!m_dirty && in_ptr == m_cached_sparam_input &&
            (!in_ptr || in_ptr->generation == m_cached_sparam_generation))
            return;
        m_dirty = false;
        m_cached_sparam_input = in_ptr;
        m_cached_input_ptr = in_ptr;
        if (in_ptr) {
            m_cached_sparam_generation = in_ptr->generation;
            m_cached_input_generation = in_ptr->generation;
        }

        auto &out = m_node.outputs[0];

        if (in_ptr && !in_ptr->frequencies.empty())
            out.frequencies = in_ptr->frequencies;
        else if (out.frequencies.size() < 2)
            buildDefaultFrequencyGrid(out.frequencies);

        out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};
        out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
        const size_t N = out.frequencies.size();

        // Apply S21 complex gain to tones
        for (auto &t : out.tones) {
            auto S = m_sparam_data.interpolate(t.freq_Hz, m_sparam_fwd_idx);
            double mag = std::abs(S);
            t.power_dBm += 20.0 * std::log10(mag);
            t.phase_deg += std::arg(S) * 180.0 / std::numbers::pi;
        }

        // Nonlinear processing
        if (m_nonlinear.enabled() && in_ptr && !in_ptr->tones.empty()) {
            size_t n_fund = out.tones.size();
            auto result = m_nonlinear.process(in_ptr->tones, [this](double freq) {
                auto S = this->m_sparam_data.interpolate(freq, this->m_sparam_fwd_idx);
                return std::abs(S);
            });
            for (const auto &t : result.extra_tones)
                out.tones.push_back(t);
            if (result.compression_dB < -1e8) {
                for (size_t i = 0; i < n_fund; ++i)
                    out.tones[i].power_dBm = MIN_POWER;
            } else {
                for (size_t i = 0; i < n_fund; ++i)
                    out.tones[i].power_dBm += result.compression_dB;
            }
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

        // Amplify input noise by |S21|^2
        out.noise_W.assign(N, 0.0);
        double sum_gain = 0.0;
        int gain_count = 0;
        for (size_t i = 0; i < N; ++i) {
            auto S = m_sparam_data.interpolate(out.frequencies[i], m_sparam_fwd_idx);
            double gain_linear = std::norm(S);
            sum_gain += gain_linear;
            ++gain_count;
            double nin =
                (in_ptr && i < in_ptr->noise_total_W.size() ? in_ptr->noise_total_W[i] : 0.0);
            out.noise_W[i] = gain_linear * nin;
        }

        // Noise figure (scaled by average S21 gain)
        double avg_gain = (gain_count > 0) ? (sum_gain / gain_count) : 1.0;
        double added_density = addedNoiseDensity_W_per_Hz(m_nf_dB, avg_gain);
        out.noise_added_W.assign(N, added_density <= 0.0 ? 0.0 : added_density);

        out.noise_total_W.resize(N);
        for (size_t i = 0; i < N; ++i)
            out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];

        out.fs_Hz = in_ptr ? in_ptr->fs_Hz : 0.0;
        out.bumpGeneration();
        return;
    }

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
    out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
    out.fs_Hz = in_ptr ? in_ptr->fs_Hz : 0.0;
    for (auto &t : out.tones) {
        t.power_dBm += m_gain_dB;
    }

    // Nonlinear processing
    if (m_nonlinear.enabled() && in_ptr && !in_ptr->tones.empty()) {
        size_t n_fund = out.tones.size();
        double gain_linear = dbToLinear(m_gain_dB);
        auto result =
            m_nonlinear.process(in_ptr->tones, [gain_linear](double) { return gain_linear; });

        for (const auto &t : result.extra_tones)
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

nlohmann::json AmplifierEngine::serialize() const {
    return {{"gain_dB", m_gain_dB},
            {"nf_dB", m_nf_dB},
            {"enable_nonlinear", m_nonlinear.enabled()},
            {"oip2_dBm", m_nonlinear.oip2_dBm()},
            {"oip3_dBm", m_nonlinear.oip3_dBm()},
            {"p1db_dBm", m_nonlinear.p1db_dBm()},
            {"sparam_mode", m_sparam_mode},
            {"sparam_filepath", m_sparam_filepath},
            {"sparam_fwd_idx", m_sparam_fwd_idx}};
}

void AmplifierEngine::deserialize(const nlohmann::json &j) {
    m_gain_dB = j.value("gain_dB", 0.0);
    m_nf_dB = j.value("nf_dB", 0.0);
    m_nonlinear.setEnabled(j.value("enable_nonlinear", false));
    m_nonlinear.setOIP2_dBm(j.value("oip2_dBm", 100.0));
    m_nonlinear.setOIP3_dBm(j.value("oip3_dBm", 100.0));
    m_nonlinear.setP1dB_dBm(j.value("p1db_dBm", 100.0));
    // Library definitions (schema v1/v2) omit `enable_nonlinear` but include
    // OIP/P1dB params. The old registry factory enabled nonlinearity whenever
    // any of those were present; project files always serialize the explicit
    // key, so only fall back when it is absent.
    if (!j.contains("enable_nonlinear") &&
        (j.contains("oip2_dBm") || j.contains("oip3_dBm") || j.contains("p1db_dBm")))
        m_nonlinear.setEnabled(true);
    m_sparam_filepath = j.value("sparam_filepath", "");
    if (!m_sparam_filepath.empty())
        m_sparam_data.load(m_sparam_filepath);
    m_sparam_mode = j.value("sparam_mode", false) && m_sparam_data.loaded();
    m_sparam_fwd_idx = j.value("sparam_fwd_idx", 0);
    m_dirty = true;
}

std::string AmplifierEngine::hoverSummary() const {
    if (m_sparam_mode && m_sparam_data.loaded()) {
        return "S-Param Amp | NF: " + std::to_string(m_nf_dB) + " dB" +
               (m_nonlinear.enabled() ? " | NL On" : "");
    }
    return "Gain: " + std::to_string(m_gain_dB) + " dB | NF: " + std::to_string(m_nf_dB) + " dB";
}
