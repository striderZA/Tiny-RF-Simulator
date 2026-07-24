#include "ideal_filter_engine.h"
#include <cstdio>
#include <nlohmann/json.hpp>
#include <numbers>

IdealFilterEngine::IdealFilterEngine(int id, NodeGraphEngine &graph) : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("IdealFilter " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
}

void IdealFilterEngine::setSParamFilepath(const std::string &path) {
    m_sparam_filepath = path;
    m_sparam_mode = m_sparam_data.load(path);
    if (m_sparam_data.loaded())
        m_sparam_fwd_idx = 1 * m_sparam_data.numPorts() + 0;
    m_dirty = true;
}

int IdealFilterEngine::inputPinId() const {
    return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1;
}

int IdealFilterEngine::outputPinId() const {
    return m_graph ? m_graph->outputPinId(m_graph_node_id) : -1;
}

bool IdealFilterEngine::isInPassband(double freq_Hz) const {
    switch (m_type) {
    case FilterType::LPF:
        return freq_Hz <= m_fc_low_Hz;
    case FilterType::HPF:
        return freq_Hz > m_fc_low_Hz;
    case FilterType::BPF:
        return freq_Hz >= m_fc_low_Hz && freq_Hz <= m_fc_high_Hz;
    case FilterType::BSF:
        return freq_Hz < m_fc_low_Hz || freq_Hz > m_fc_high_Hz;
    }
    return true;
}

void IdealFilterEngine::update(double dt) {
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

        const size_t N = out.frequencies.size();
        int idx = m_sparam_fwd_idx;

        // Apply S21 to tones
        out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};
        for (auto &t : out.tones) {
            auto S = m_sparam_data.interpolate(t.freq_Hz, idx);
            t.power_dBm += 20.0 * std::log10(std::abs(S));
            t.phase_deg += std::arg(S) * 180.0 / std::numbers::pi;
        }

        // Phase
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

        // Filter noise by |S21|^2, no added noise
        out.noise_W.assign(N, 0.0);
        for (size_t i = 0; i < N; ++i) {
            auto S = m_sparam_data.interpolate(out.frequencies[i], idx);
            double nin =
                (in_ptr && i < in_ptr->noise_total_W.size() ? in_ptr->noise_total_W[i] : 0.0);
            out.noise_W[i] = std::norm(S) * nin;
        }
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W = out.noise_W;

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

    const size_t N = out.frequencies.size();

    out.tones.clear();
    if (in_ptr) {
        for (const auto &t : in_ptr->tones) {
            if (isInPassband(t.freq_Hz))
                out.tones.push_back(t);
        }
    }

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

    out.noise_W.resize(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        if (isInPassband(out.frequencies[i]))
            out.noise_W[i] = (in_ptr && i < in_ptr->noise_W.size()) ? in_ptr->noise_W[i] : 0.0;
    }

    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i)
        out.noise_total_W[i] = out.noise_W[i];

    out.fs_Hz = in_ptr ? in_ptr->fs_Hz : 0.0;
    out.bumpGeneration();
}

nlohmann::json IdealFilterEngine::serialize() const {
    return {{"filter_type", static_cast<int>(m_type)},
            {"fc_low_Hz", m_fc_low_Hz},
            {"fc_high_Hz", m_fc_high_Hz},
            {"sparam_mode", m_sparam_mode},
            {"sparam_filepath", m_sparam_filepath},
            {"sparam_fwd_idx", m_sparam_fwd_idx}};
}

void IdealFilterEngine::deserialize(const nlohmann::json &j) {
    int ft = j.value("filter_type", 0);
    if (ft < 0)
        ft = 0;
    if (ft > 3)
        ft = 3;
    m_type = static_cast<FilterType>(ft);
    m_fc_low_Hz = j.value("fc_low_Hz", 100e6);
    m_fc_high_Hz = j.value("fc_high_Hz", 200e6);
    m_sparam_mode = j.value("sparam_mode", false);
    m_sparam_filepath = j.value("sparam_filepath", "");
    m_sparam_fwd_idx = j.value("sparam_fwd_idx", 0);
    m_dirty = true;
}

std::string IdealFilterEngine::hoverSummary() const {
    if (m_sparam_mode && m_sparam_data.loaded()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "S-Param Filter | %zu pts", m_sparam_data.freqs().size());
        return buf;
    }

    const char *type_names[] = {"LPF", "HPF", "BPF", "BSF"};
    const char *tn = type_names[static_cast<int>(m_type)];
    char buf[128];
    if (m_type == FilterType::BPF || m_type == FilterType::BSF) {
        std::snprintf(buf, sizeof(buf), "Ideal %s | %.1f-%.1f MHz", tn, m_fc_low_Hz / 1e6,
                      m_fc_high_Hz / 1e6);
    } else {
        std::snprintf(buf, sizeof(buf), "Ideal %s | fc=%.1f MHz", tn, m_fc_low_Hz / 1e6);
    }
    return buf;
}
