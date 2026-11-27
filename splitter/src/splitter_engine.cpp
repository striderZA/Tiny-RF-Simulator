#include "splitter_engine.h"
#include "common.h"
#include <cmath>
#include <nlohmann/json.hpp>
#include <numbers>

SplitterEngine::SplitterEngine(int id, NodeGraphEngine &graph)
    : ComponentEngineBase(id, graph, "Splitter", 1, 2) {}

int SplitterEngine::outputPinId(int index) const {
    if (!m_graph || m_graph_node_id < 0)
        return -1;
    for (const auto &node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            if (index < 0 || static_cast<size_t>(index) >= node.output_pin_ids.size())
                return -1;
            return node.output_pin_ids[index];
        }
    }
    return -1;
}

void SplitterEngine::setSParamMode(bool enabled) {
    // Allow enabling before a file is chosen so the inspector can reveal its
    // picker. A loaded file must be exactly 3-port; update() also enforces this.
    m_sparam_mode = enabled && (!m_sparam_data.loaded() || m_sparam_data.numPorts() == 3);
    m_dirty = true;
}

void SplitterEngine::setSParamFilepath(const std::string &path) {
    m_sparam_filepath = path;
    m_sparam_mode = m_sparam_data.load(path) && m_sparam_data.numPorts() == 3;
    m_dirty = true;
}

std::string SplitterEngine::hoverSummary() const { return "Split: -3 dB | 1 in, 2 out"; }

nlohmann::json SplitterEngine::serialize() const {
    return {{"sparam_mode", m_sparam_mode}, {"sparam_filepath", m_sparam_filepath}};
}

void SplitterEngine::deserialize(const nlohmann::json &j) {
    m_sparam_filepath = j.value("sparam_filepath", j.value("sparam_path", ""));
    if (!m_sparam_filepath.empty())
        m_sparam_data.load(m_sparam_filepath);
    m_sparam_mode =
        j.value("sparam_mode", false) && m_sparam_data.loaded() && m_sparam_data.numPorts() == 3;
    m_dirty = true;
}

void SplitterEngine::update(double dt) {
    (void)dt;
    const Spectrum *in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];

    // --- S-parameter mode ---
    // A non-3-port file must fall through to manual mode: interpolate() bounds-checks
    // and would otherwise resolve S21/S31 to {1,0} and produce the k*T*(1-1-1)
    // added-noise artifact (combiner issue #117/#137 precedent).
    if (m_sparam_mode && m_sparam_data.loaded() && m_sparam_data.numPorts() == 3) {
        if (!beginUpdate(in_ptr))
            return;

        // 3-port device: port 0 = input, ports 1 and 2 = outputs.
        // S21: input -> output 0, S31: input -> output 1 (row-major, (row, col) = (out, in)).
        const int idx_S21 = 1 * m_sparam_data.numPorts() + 0;
        const int idx_S31 = 2 * m_sparam_data.numPorts() + 0;

        for (size_t out_idx = 0; out_idx < m_node.outputs.size(); ++out_idx) {
            auto &out = m_node.outputs[out_idx];
            const int param_idx = out_idx == 0 ? idx_S21 : idx_S31;

            if (in_ptr && !in_ptr->frequencies.empty())
                out.frequencies = in_ptr->frequencies;
            else if (out.frequencies.size() < 2)
                buildDefaultFrequencyGrid(out.frequencies);

            const size_t N = out.frequencies.size();

            out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
            out.fs_Hz = in_ptr ? in_ptr->fs_Hz : 0.0;

            out.tones.clear();
            if (in_ptr) {
                for (const auto &t : in_ptr->tones) {
                    auto S = m_sparam_data.interpolate(t.freq_Hz, param_idx);
                    Spectrum::Tone t_out = t;
                    t_out.power_dBm += 20.0 * std::log10(std::abs(S));
                    t_out.phase_deg += std::arg(S) * 180.0 / std::numbers::pi;
                    out.tones.push_back(t_out);
                }
            }

            if (N < 2) {
                out.noise_W.assign(N, 0.0);
                out.noise_added_W.assign(N, 0.0);
                out.noise_total_W.assign(N, 0.0);
                out.phase_deg.assign(N, 0.0);
                out.bumpGeneration();
                continue;
            }

            out.noise_W.assign(N, 0.0);
            out.noise_added_W.assign(N, 0.0);
            out.noise_total_W.resize(N);
            out.phase_deg.assign(N, 0.0);

            for (size_t i = 0; i < N; ++i) {
                auto S = m_sparam_data.interpolate(out.frequencies[i], param_idx);
                const double gain = std::norm(S);
                const double nin =
                    (in_ptr && i < in_ptr->noise_total_W.size() ? in_ptr->noise_total_W[i] : 0.0);
                // Passive lossy stage: input noise times power gain plus kT(1-gain),
                // matching the manual mode's attenuator rule (and the combiner's).
                out.noise_W[i] = gain * nin;
                out.noise_added_W[i] = k * T * (1.0 - gain);
                out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];

                const double base =
                    (in_ptr && i < in_ptr->phase_deg.size() ? in_ptr->phase_deg[i] : 0.0);
                out.phase_deg[i] = base + std::arg(S) * 180.0 / std::numbers::pi;
            }

            m_sparam_data.markOutOfBand(in_ptr, out);
            out.bumpGeneration();
        }
        return;
    }

    // --- Manual (ideal) mode ---
    if (!beginUpdate(in_ptr))
        return;

    for (size_t out_idx = 0; out_idx < m_node.outputs.size(); ++out_idx) {
        auto &out = m_node.outputs[out_idx];

        if (in_ptr && !in_ptr->frequencies.empty()) {
            out.frequencies = in_ptr->frequencies;
        } else if (out.frequencies.size() < 2) {
            buildDefaultFrequencyGrid(out.frequencies);
        }

        const size_t N = out.frequencies.size();

        out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};
        out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
        out.fs_Hz = in_ptr ? in_ptr->fs_Hz : 0.0;
        // The attenuator precedent: a stage in its ideal/manual mode carries its input's
        // marks through (they are upstream facts), so this output drops any marks a
        // previous S-parameter frame set here.
        out.out_of_band = in_ptr ? in_ptr->out_of_band : std::vector<bool>{};
        for (auto &t : out.tones) {
            t.power_dBm -= SPLIT_LOSS_DB;
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
            continue;
        }

        double loss_linear = dbToLinear(-SPLIT_LOSS_DB);

        out.noise_W.assign(N, 0.0);
        for (size_t i = 0; i < N; ++i) {
            double nin =
                (in_ptr && i < in_ptr->noise_total_W.size() ? in_ptr->noise_total_W[i] : 0.0);
            out.noise_W[i] = loss_linear * nin;
        }

        // Passive lossy split: each output adds kT(1-gain), as the attenuator does.
        out.noise_added_W.assign(N, k * T * (1.0 - loss_linear));
        out.noise_total_W.resize(N);
        for (size_t i = 0; i < N; ++i) {
            out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
        }
    }

    for (auto &out : m_node.outputs)
        out.bumpGeneration();
}
