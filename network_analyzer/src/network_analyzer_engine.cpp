#include "network_analyzer_engine.h"

#include "common.h"
#include "component_interface.h"
#include "node_graph_engine.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>

NetworkAnalyzerEngine::NetworkAnalyzerEngine(NodeGraphEngine &graph, INetworkAnalyzerHost &host)
    : m_graph(graph), m_host(host) {}

void NetworkAnalyzerEngine::setStartFrequency(double hz) {
    if (hz != m_start_freq) {
        m_start_freq = hz;
        m_sweep_params_dirty = true;
    }
}

void NetworkAnalyzerEngine::setStopFrequency(double hz) {
    if (hz != m_stop_freq) {
        m_stop_freq = hz;
        m_sweep_params_dirty = true;
    }
}

void NetworkAnalyzerEngine::setPoints(int n) {
    int clamped = std::clamp(n, 2, 2001);
    if (clamped != m_points) {
        m_points = clamped;
        m_sweep_params_dirty = true;
    }
}

void NetworkAnalyzerEngine::setStimulusPower(double dBm) {
    if (dBm != m_stimulus_power_dBm) {
        m_stimulus_power_dBm = dBm;
        m_sweep_params_dirty = true;
    }
}

void NetworkAnalyzerEngine::setPointA(int pin_id) { m_point_a_pin = pin_id; }
void NetworkAnalyzerEngine::setPointB(int pin_id) { m_point_b_pin = pin_id; }

void NetworkAnalyzerEngine::rebuildStimulus() {
    const int n = m_points;
    m_stimulus_freqs.resize(static_cast<size_t>(n));
    const double span = m_stop_freq - m_start_freq;
    for (int i = 0; i < n; ++i) {
        double t = (n > 1) ? static_cast<double>(i) / (n - 1) : 0.0;
        m_stimulus_freqs[static_cast<size_t>(i)] = m_start_freq + span * t;
    }

    auto &out = m_stimulus;
    out.frequencies = m_stimulus_freqs;
    out.tones.resize(m_stimulus_freqs.size());
    for (size_t i = 0; i < m_stimulus_freqs.size(); ++i)
        out.tones[i] = {m_stimulus_freqs[i], m_stimulus_power_dBm, 0.0};

    out.noise_W.assign(m_stimulus_freqs.size(), k * T);
    out.noise_added_W.assign(m_stimulus_freqs.size(), 0.0);
    out.phase_deg.assign(m_stimulus_freqs.size(), 0.0);
    out.computeTotalNoise();
    out.fs_Hz = 0.0;
    out.is_complex_baseband = false;
    out.bumpGeneration();

    m_gain_dB.assign(m_stimulus_freqs.size(), std::numeric_limits<double>::quiet_NaN());
    m_nf_dB.assign(m_stimulus_freqs.size(), std::numeric_limits<double>::quiet_NaN());
}

std::optional<std::vector<IComponentEngine *>> NetworkAnalyzerEngine::findUniquePath() const {
    const int start_node = m_graph.nodeIdForPin(m_point_a_pin);
    const int end_node = m_graph.nodeIdForPin(m_point_b_pin);
    if (start_node < 0 || end_node < 0 || start_node == end_node)
        return std::nullopt;

    // Forward adjacency: node -> nodes reachable via one real output link.
    // Duplicate links (same start pin -> same end pin, which the graph allows)
    // collapse into a single edge so they cannot fake an ambiguous path.
    std::unordered_map<int, std::vector<int>> next_of;
    for (const auto &node : m_graph.nodes()) {
        std::vector<int> nexts;
        for (int out_pin : node.output_pin_ids) {
            for (const auto &link : m_graph.links()) {
                if (link.start_pin_id == out_pin) {
                    int nxt = m_graph.nodeIdForPin(link.end_pin_id);
                    if (nxt >= 0)
                        nexts.push_back(nxt);
                }
            }
        }
        std::sort(nexts.begin(), nexts.end());
        nexts.erase(std::unique(nexts.begin(), nexts.end()), nexts.end());
        if (!nexts.empty())
            next_of.emplace(node.node_id, std::move(nexts));
    }

    // A chain that crosses a Combiner's combined signal input is inherently
    // ambiguous (the combiner merges two primary paths; the private chain can
    // only reproduce one) -> reject. The start node is exempt: its output is
    // the injection point, replaced by the stimulus, so the combiner itself is
    // upstream of the measurement and never part of the chain.
    const auto crosses_combiner = [&](int node_id) {
        auto *comp = m_host.componentForNode(node_id);
        return comp && comp->type_name() == "combiner";
    };

    // DFS enumerating distinct simple paths, bailing out as soon as a second
    // one is found (only "unique or not" matters — no exhaustive enumeration,
    // so large graphs cannot blow up).
    std::vector<int> path;
    std::vector<int> found_path;
    int path_count = 0;

    std::function<void(int)> dfs = [&](int node) {
        if (path_count >= 2)
            return;
        if (node == end_node) {
            ++path_count;
            found_path = path;
            return;
        }
        auto it = next_of.find(node);
        if (it == next_of.end())
            return;
        for (int nxt : it->second) {
            if (path_count >= 2)
                return;
            if (crosses_combiner(nxt))
                continue;
            if (std::find(path.begin(), path.end(), nxt) != path.end())
                continue; // simple paths only (no revisits)
            path.push_back(nxt);
            dfs(nxt);
            path.pop_back();
        }
    };

    path.push_back(start_node);
    dfs(start_node);

    if (path_count != 1)
        return std::nullopt; // no path, or more than one distinct path

    std::vector<IComponentEngine *> result;
    result.reserve(found_path.size());
    for (int node_id : found_path) {
        auto *comp = m_host.componentForNode(node_id);
        if (!comp)
            return std::nullopt; // unregistered node — cannot clone it
        result.push_back(comp);
    }
    return result;
}

void NetworkAnalyzerEngine::computeMeasurement() {
    const size_t N = m_stimulus_freqs.size();
    m_gain_dB.assign(N, std::numeric_limits<double>::quiet_NaN());
    m_nf_dB.assign(N, std::numeric_limits<double>::quiet_NaN());

    auto path = findUniquePath();
    // No path, ambiguous path, or Point A == Point B -> all-NaN. The chain
    // must contain at least one component: the stimulus is injected at Point
    // A's output, which replaces the start node's own output.
    if (!path || path->size() < 2)
        return;

    // Private, throwaway clone chain: a fresh scratch graph+registry for this
    // pass only, destroyed at function exit. The real graph/registry are never
    // read for signal purposes and never written to.
    auto scratch = m_host.beginScratchPass();
    if (!scratch)
        return;

    // Chain = path nodes after Point A's node, through Point B's node (the
    // stimulus replaces Point A's signal, so A's own node is not measured).
    std::vector<IComponentEngine *> clones;
    clones.reserve(path->size() - 1);
    for (size_t i = 1; i < path->size(); ++i) {
        IComponentEngine *real = (*path)[i];
        IComponentEngine *clone = scratch->createClone(real->type_name(), real->id());
        if (!clone)
            return; // unknown type -> no data
        clone->deserialize(real->serialize());
        clones.push_back(clone);
    }

    // Wire by directly assigning SignalNode* pointers (no scratch-graph links
    // needed), the same technique RfSimulatorApp::rewireInputs() uses.
    for (size_t i = 0; i < clones.size(); ++i) {
        auto &inputs = clones[i]->node().inputs;
        if (inputs.empty())
            return; // a chain component must have an input to feed
        inputs[0] = (i == 0) ? &m_stimulus : &clones[i - 1]->node().outputs[0];
        // Mixer clones borrow the real, live LO input (read-only). The current
        // MixerEngine has a single RF input (the LO is the lo_freq_Hz
        // parameter, already copied exactly by deserialize(real->serialize())),
        // so this is a no-op today; it keeps the clone faithful if a mixer
        // model with a separate LO signal input appears.
        if (clones[i]->type_name() == "mixer" && inputs.size() > 1) {
            const auto &real_inputs = (*path)[i + 1]->node().inputs;
            if (real_inputs.size() > 1)
                inputs[1] = real_inputs[1];
        }
    }

    for (auto *clone : clones)
        clone->update(0.0);

    // Match tones by frequency value against the last clone's output, exactly
    // like the prior gain/NF formula (k/T/dbToLinear reused from common.h; no
    // Friis math re-derived). A dropped/mistranslated point degrades to NaN.
    const Spectrum *response = &clones.back()->node().outputs[0];
    constexpr double kFreqEpsilonHz = 1.0;
    for (size_t i = 0; i < N; ++i) {
        const double f = m_stimulus_freqs[i];

        const Spectrum::Tone *matched_tone = nullptr;
        for (const auto &t : response->tones) {
            if (std::abs(t.freq_Hz - f) <= kFreqEpsilonHz) {
                matched_tone = &t;
                break;
            }
        }
        if (!matched_tone)
            continue;

        const double gain_dB = matched_tone->power_dBm - m_stimulus_power_dBm;
        if (gain_dB < -100.0)
            continue; // indistinguishable from noise floor -> no data

        size_t noise_idx = response->frequencies.size();
        for (size_t j = 0; j < response->frequencies.size(); ++j) {
            if (std::abs(response->frequencies[j] - f) <= kFreqEpsilonHz) {
                noise_idx = j;
                break;
            }
        }

        m_gain_dB[i] = gain_dB;

        if (noise_idx < response->noise_total_W.size()) {
            const double gain_linear = dbToLinear(gain_dB);
            const double noise_out_W = response->noise_total_W[noise_idx];
            const double nf_linear = (noise_out_W / gain_linear) / (k * T);
            if (nf_linear > 0.0)
                m_nf_dB[i] = 10.0 * std::log10(nf_linear);
        }
    }
}

void NetworkAnalyzerEngine::update() {
    if (m_sweep_params_dirty) {
        rebuildStimulus();
        m_sweep_params_dirty = false;
    }
    computeMeasurement();
}

nlohmann::json NetworkAnalyzerEngine::serialize() const {
    return {{"start_freq_hz", m_start_freq},
            {"stop_freq_hz", m_stop_freq},
            {"points", m_points},
            {"stimulus_power_dBm", m_stimulus_power_dBm},
            {"point_a_pin", m_point_a_pin},
            {"point_b_pin", m_point_b_pin}};
}

void NetworkAnalyzerEngine::deserialize(const nlohmann::json &j) {
    m_start_freq = j.value("start_freq_hz", 1e9);
    m_stop_freq = j.value("stop_freq_hz", 6e9);
    m_points = std::clamp(j.value("points", 201), 2, 2001);
    m_stimulus_power_dBm = j.value("stimulus_power_dBm", -30.0);
    m_point_a_pin = j.value("point_a_pin", -1);
    m_point_b_pin = j.value("point_b_pin", -1);
    m_sweep_params_dirty = true;
}
