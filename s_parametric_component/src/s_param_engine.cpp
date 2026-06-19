#include "s_param_engine.h"
#include "common.h"
#include "logging_core.h"
#include <algorithm>
#include <cmath>

SParamEngine::SParamEngine(int id, NodeGraphEngine& graph,
                           const std::string& filepath)
    : m_id(id), m_graph(&graph), m_filepath(filepath) {
    // Create placeholder node — will be rebuilt on first successful reload
    m_graph_node_id = graph.addNode("S-Param " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
    reload(filepath);
}

void SParamEngine::rebuildNode() {
    int np = m_data.numPorts();
    if (np < 1) return;

    // Remove old graph node if it exists
    if (m_graph_node_id >= 0) {
        m_graph->removeNode(m_graph_node_id);
    }

    // Create new node with np inputs and np outputs
    m_graph_node_id = m_graph->addNode("S-Param " + std::to_string(m_id), &m_node, np, np);
    m_node.inputs.resize(np);
    m_node.outputs.resize(np);

    // Set pin labels
    {
        std::vector<std::string> in_labels(np), out_labels(np);
        for (int p = 0; p < np; ++p) {
            in_labels[p] = "Port " + std::to_string(p + 1);
            out_labels[p] = "Port " + std::to_string(p + 1);
        }
        m_graph->setNodePinLabels(m_graph_node_id, in_labels, out_labels);
    }

    m_cache_valid = false;
    LOG_INFO("Rebuilt S-param node %d with %d ports", m_id, np);
}

void SParamEngine::reload(const std::string& filepath) {
    m_filepath = filepath;
    m_forward_param_idx = -1; // -1 = full matrix mode (new default)

    if (!m_data.load(filepath))
        return;

    int np = m_data.numPorts();

    // Rebuild graph node if pin count changed
    bool need_rebuild = (static_cast<int>(m_node.inputs.size()) != np);
    if (need_rebuild) {
        rebuildNode();
    }

    m_forward_param_idx = -1; // default: full matrix
    m_dirty = true;
    m_cache_valid = false;

    LOG_INFO("Loaded S-parameter component %d from %s (%zu points, %d ports)",
             m_id, filepath.c_str(), m_data.freqs().size(), np);
}

int SParamEngine::inputPinId(int port) const {
    if (!m_graph || m_graph_node_id < 0) return -1;
    for (const auto& node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            if (port >= 0 && static_cast<size_t>(port) < node.input_pin_ids.size())
                return node.input_pin_ids[port];
            break;
        }
    }
    return -1;
}

int SParamEngine::outputPinId(int port) const {
    if (!m_graph || m_graph_node_id < 0) return -1;
    for (const auto& node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            if (port >= 0 && static_cast<size_t>(port) < node.output_pin_ids.size())
                return node.output_pin_ids[port];
            break;
        }
    }
    return -1;
}

void SParamEngine::setForwardParamIdx(int idx) {
    int total = m_data.paramCount();
    if ((idx >= 0 && idx < total) || idx == -1) {
        if (idx != m_forward_param_idx) {
            m_forward_param_idx = idx;
            m_dirty = true;
        }
    }
}

void SParamEngine::update(double dt) {
    (void)dt;

    if (!m_data.loaded()) {
        for (auto& out : m_node.outputs)
            out.bumpGeneration();
        return;
    }

    int N = m_data.numPorts();
    if (N < 1) {
        for (auto& out : m_node.outputs)
            out.bumpGeneration();
        return;
    }

    // --- Cache check ---
    bool inputs_unchanged = !m_dirty && m_cache_valid;
    if (inputs_unchanged) {
        for (int k = 0; k < N; ++k) {
            const Spectrum* in_k = (static_cast<size_t>(k) < m_node.inputs.size())
                ? m_node.inputs[k] : nullptr;
            if (in_k != m_cached_input_ptrs[k] ||
                (in_k && in_k->generation != m_cached_input_generations[k])) {
                inputs_unchanged = false;
                break;
            }
        }
    }
    if (inputs_unchanged)
        return;

    m_dirty = false;
    m_cache_valid = true;

    // Update cache
    m_cached_input_ptrs.resize(N);
    m_cached_input_generations.resize(N);
    for (int k = 0; k < N; ++k) {
        const Spectrum* in_k = (static_cast<size_t>(k) < m_node.inputs.size())
            ? m_node.inputs[k] : nullptr;
        m_cached_input_ptrs[k] = in_k;
        m_cached_input_generations[k] = in_k ? in_k->generation : 0;
    }

    // --- Legacy single-param mode ---
    if (m_forward_param_idx >= 0) {
        const Spectrum* in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
        Spectrum empty_in;
        const Spectrum& in = in_ptr ? *in_ptr : empty_in;
        m_data.applyToSpectrum(in, m_node.outputs[0], m_forward_param_idx);

        // Clear other outputs
        for (int j = 1; j < N; ++j) {
            m_node.outputs[j].frequencies.clear();
            m_node.outputs[j].tones.clear();
            m_node.outputs[j].noise_W.clear();
            m_node.outputs[j].noise_added_W.clear();
            m_node.outputs[j].noise_total_W.clear();
            m_node.outputs[j].phase_deg.clear();
            m_node.outputs[j].bumpGeneration();
        }

        // Noise figure
        if (m_nf_dB > 0.0 && !m_node.outputs[0].frequencies.empty() && m_node.outputs[0].frequencies.size() >= 2) {
            size_t Nf = m_node.outputs[0].frequencies.size();
            double Te = calculateNoiseTemp(m_nf_dB);
            m_node.outputs[0].noise_added_W.resize(Nf);
            for (size_t i = 0; i < Nf; ++i) {
                auto S = m_data.interpolate(m_node.outputs[0].frequencies[i], m_forward_param_idx);
                double gain_linear = std::norm(S);
                m_node.outputs[0].noise_added_W[i] = k * Te * gain_linear;
                m_node.outputs[0].noise_total_W[i] = m_node.outputs[0].noise_W[i] + m_node.outputs[0].noise_added_W[i];
            }
        }

        // Nonlinear processing
        if (m_nonlinear.enabled() && in_ptr && !in_ptr->tones.empty()) {
            size_t n_fund = m_node.outputs[0].tones.size();
            auto result = m_nonlinear.process(in_ptr->tones,
                [this](double freq) {
                    auto S = this->m_data.interpolate(freq, m_forward_param_idx);
                    return std::abs(S);
                });

            for (const auto& t : result.extra_tones)
                m_node.outputs[0].tones.push_back(t);

            if (result.compression_dB < -1e8) {
                for (size_t kk = 0; kk < n_fund; ++kk)
                    m_node.outputs[0].tones[kk].power_dBm = MIN_POWER;
            } else {
                for (size_t kk = 0; kk < n_fund; ++kk)
                    m_node.outputs[0].tones[kk].power_dBm += result.compression_dB;
            }
        }

        m_node.outputs[0].bumpGeneration();
        return;
    }

    // --- Full matrix mode ---
    for (int j = 0; j < N; ++j) {
        Spectrum& out = m_node.outputs[j];

        // Determine frequency grid from first connected input
        const Spectrum* grid_source = nullptr;
        for (int k = 0; k < N; ++k) {
            if (m_node.inputs[k]) {
                grid_source = m_node.inputs[k];
                break;
            }
        }

        if (grid_source && !grid_source->frequencies.empty()) {
            out.frequencies = grid_source->frequencies;
        } else if (out.frequencies.size() < 2) {
            buildDefaultFrequencyGrid(out.frequencies);
        }

        const size_t n_bins = out.frequencies.size();

        // Clear output accumulators
        out.tones.clear();

        if (grid_source && !grid_source->phase_deg.empty()) {
            out.phase_deg = grid_source->phase_deg;
        } else {
            out.phase_deg.assign(n_bins, 0.0);
        }

        out.noise_W.assign(n_bins, 0.0);
        out.noise_added_W.assign(n_bins, 0.0);

        // Accumulate contributions from all connected input ports
        for (int k = 0; k < N; ++k) {
            const Spectrum* in_k = m_node.inputs[k];
            if (!in_k) continue;

            int param_idx = j * N + k; // row-major: S_jk

            // Tones: apply S_jk complex gain to each tone
            for (const auto& tone : in_k->tones) {
                auto S = m_data.interpolate(tone.freq_Hz, param_idx);
                double mag = std::abs(S);
                double phase_shift = std::arg(S) * 180.0 / std::numbers::pi;

                Spectrum::Tone t_out;
                t_out.freq_Hz = tone.freq_Hz;
                t_out.power_dBm = tone.power_dBm + 20.0 * std::log10(mag);
                t_out.phase_deg = tone.phase_deg + phase_shift;

                // Merge with existing tone at same frequency (coherent addition)
                bool merged = false;
                for (auto& existing : out.tones) {
                    if (std::abs(existing.freq_Hz - t_out.freq_Hz) < 1.0) {
                        // Convert to complex amplitude, add, convert back
                        double a1 = std::pow(10.0, existing.power_dBm / 20.0);
                        double p1 = existing.phase_deg * std::numbers::pi / 180.0;
                        double a2 = std::pow(10.0, t_out.power_dBm / 20.0);
                        double p2 = t_out.phase_deg * std::numbers::pi / 180.0;

                        double re = a1 * std::cos(p1) + a2 * std::cos(p2);
                        double im = a1 * std::sin(p1) + a2 * std::sin(p2);

                        existing.power_dBm = 20.0 * std::log10(std::sqrt(re * re + im * im));
                        existing.phase_deg = std::atan2(im, re) * 180.0 / std::numbers::pi;
                        merged = true;
                        break;
                    }
                }
                if (!merged) {
                    out.tones.push_back(t_out);
                }
            }

            // Noise: uncorrelated -> add power (|S_jk|^2 x input_noise)
            if (in_k->noise_total_W.empty())
                continue;

            for (size_t i = 0; i < n_bins && i < in_k->noise_total_W.size(); ++i) {
                auto S = m_data.interpolate(out.frequencies[i], param_idx);
                double gain_linear = std::norm(S);
                out.noise_W[i] += gain_linear * in_k->noise_total_W[i];
            }
        }

        // Noise figure (applied per output port)
        if (m_nf_dB > 0.0 && n_bins >= 2) {
            double Te = calculateNoiseTemp(m_nf_dB);
            for (size_t i = 0; i < n_bins; ++i) {
                // Compute total gain into this output port
                double sum_gain = 0.0;
                for (int k = 0; k < N; ++k) {
                    if (!m_node.inputs[k]) continue;
                    auto S = m_data.interpolate(out.frequencies[i], j * N + k);
                    sum_gain += std::norm(S);
                }
                out.noise_added_W[i] = k * Te * sum_gain;
            }
        } else if (n_bins >= 2) {
            out.noise_added_W.assign(n_bins, 0.0);
        }

        if (n_bins >= 2) {
            out.noise_total_W.resize(n_bins);
            for (size_t i = 0; i < n_bins; ++i)
                out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
        }

        out.bumpGeneration();
    }

    // Apply nonlinearity on primary output only (port 0)
    // Multi-port nonlinearity is future work
    if (m_nonlinear.enabled() && !m_node.inputs.empty()) {
        const Spectrum* in_ptr = m_node.inputs[0];
        if (in_ptr && !in_ptr->tones.empty()) {
            size_t n_fund = m_node.outputs[0].tones.size();
            auto result = m_nonlinear.process(in_ptr->tones,
                [this](double freq) {
                    auto S = this->m_data.interpolate(freq, 0);
                    return std::abs(S);
                });

            for (const auto& t : result.extra_tones)
                m_node.outputs[0].tones.push_back(t);

            if (result.compression_dB < -1e8) {
                for (size_t kk = 0; kk < n_fund; ++kk)
                    m_node.outputs[0].tones[kk].power_dBm = MIN_POWER;
            } else {
                for (size_t kk = 0; kk < n_fund; ++kk)
                    m_node.outputs[0].tones[kk].power_dBm += result.compression_dB;
            }
        }
    }
}

std::string SParamEngine::hoverSummary() const {
    if (!m_data.loaded()) return "Not loaded";
    int np = m_data.numPorts();
    std::string s = std::to_string(np) + "-port";
    if (m_forward_param_idx < 0) {
        s += " | Full Matrix";
    } else {
        s += " | S"
            + std::to_string((m_forward_param_idx / np) + 1)
            + std::to_string((m_forward_param_idx % np) + 1);
    }
    if (m_nf_dB > 0.0)
        s += " | NF: " + std::to_string(m_nf_dB) + " dB";
    if (m_nonlinear.enabled())
        s += " | OIP2: " + std::to_string(m_nonlinear.oip2_dBm())
           + " OIP3: " + std::to_string(m_nonlinear.oip3_dBm());
    return s;
}
