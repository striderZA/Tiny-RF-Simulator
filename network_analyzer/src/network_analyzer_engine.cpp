#include "network_analyzer_engine.h"

#include "common.h"
#include "component_interface.h"
#include "node_graph_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <optional>
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

std::optional<NetworkAnalyzerEngine::PathResult> NetworkAnalyzerEngine::findUniquePath() const {
    const int start_node = m_graph.nodeIdForPin(m_point_a_pin);
    const int end_node = m_graph.nodeIdForPin(m_point_b_pin);
    if (start_node < 0 || end_node < 0 || start_node == end_node)
        return std::nullopt;

    // Forward adjacency: node -> [(output port index, node reachable via that
    // port's real link)]. The port index is kept (not just the next node) so
    // the clone chain later reads the SAME output a multi-output component
    // (e.g. a PFB Channelizer's two structurally different outputs) actually
    // used on the real graph, instead of guessing port 0. Duplicate links
    // (same start pin -> same end pin, which the graph allows) collapse into
    // a single edge so they cannot fake an ambiguous path.
    std::unordered_map<int, std::vector<std::pair<int, int>>> next_of;
    for (const auto &node : m_graph.nodes()) {
        std::vector<std::pair<int, int>> nexts;
        for (size_t oi = 0; oi < node.output_pin_ids.size(); ++oi) {
            const int out_pin = node.output_pin_ids[oi];
            for (const auto &link : m_graph.links()) {
                if (link.start_pin_id == out_pin) {
                    const int nxt = m_graph.nodeIdForPin(link.end_pin_id);
                    if (nxt >= 0)
                        nexts.emplace_back(static_cast<int>(oi), nxt);
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
    // one is found (only "unique or not" matters). Also capped by total
    // visited steps: `path_count >= 2` alone only bounds successful paths, not
    // wasted exploration of branches that never reach end_node — a graph with
    // many dead-end branches could otherwise still visit an exponential
    // number of simple-path prefixes. 100000 is far beyond any plausible
    // project size; hitting it degrades to "no unique path" (no-data),
    // consistent with every other ambiguous/unsupported topology here.
    constexpr int kMaxDfsSteps = 100000;
    std::vector<int> path;
    std::vector<int> path_out_idx; // path_out_idx[j] = output port path[j] used
                                   // to reach path[j+1]; size = path.size()-1
    std::vector<int> found_path;
    std::vector<int> found_out_idx;
    int path_count = 0;
    int steps = 0;

    std::function<void(int)> dfs = [&](int node) {
        if (path_count >= 2 || ++steps > kMaxDfsSteps)
            return;
        if (node == end_node) {
            ++path_count;
            found_path = path;
            found_out_idx = path_out_idx;
            return;
        }
        auto it = next_of.find(node);
        if (it == next_of.end())
            return;
        for (const auto &[oi, nxt] : it->second) {
            if (path_count >= 2 || steps > kMaxDfsSteps)
                return;
            if (crosses_combiner(nxt))
                continue;
            if (std::find(path.begin(), path.end(), nxt) != path.end())
                continue; // simple paths only (no revisits)
            path.push_back(nxt);
            path_out_idx.push_back(oi);
            dfs(nxt);
            path_out_idx.pop_back();
            path.pop_back();
        }
    };

    path.push_back(start_node);
    dfs(start_node);

    if (path_count != 1)
        return std::nullopt; // no path, or more than one distinct path

    PathResult result;
    result.nodes.reserve(found_path.size());
    for (int node_id : found_path) {
        auto *comp = m_host.componentForNode(node_id);
        if (!comp)
            return std::nullopt; // unregistered node — cannot clone it
        result.nodes.push_back(comp);
    }
    result.out_index = std::move(found_out_idx);
    return result;
}

void NetworkAnalyzerEngine::computeMeasurement() {
    const size_t N = m_stimulus_freqs.size();

    auto path = findUniquePath();
    // No path, ambiguous path, or Point A == Point B -> all-NaN. The chain
    // must contain at least one component: the stimulus is injected at Point
    // A's output, which replaces the start node's own output.
    if (!path || path->nodes.size() < 2) {
        m_gain_dB.assign(N, std::numeric_limits<double>::quiet_NaN());
        m_nf_dB.assign(N, std::numeric_limits<double>::quiet_NaN());
        m_cached_signature.clear();
        return;
    }

    // Dirty-check: everything below here re-runs each path component's full
    // DSP across up to 2001 points -- real, non-trivial work (a nonlinear
    // stage's harmonics/IMD generation scales with tone count) -- and this
    // function runs unconditionally every ImGui frame the panel is visible.
    // This instrument has no wired input to compare a cached pointer +
    // generation against (every other engine's dirty-check), since it reads
    // a chain of REAL, externally-owned components it gets no change
    // notification from. Stand in with a signature of the discovered chain
    // (each node's live serialize() dump -- %.17g-precise doubles round-trip
    // exactly, so this only changes when a value actually changes) plus the
    // sweep params and both probe pins; skip the clone-and-cascade below,
    // reusing last frame's m_gain_dB/m_nf_dB, when nothing in it moved.
    std::string signature;
    signature.reserve(256);
    for (size_t i = 0; i < path->nodes.size(); ++i) {
        signature += path->nodes[i]->type_name();
        signature += ':';
        signature += std::to_string(path->nodes[i]->id());
        signature += path->nodes[i]->serialize().dump();
        if (i < path->out_index.size())
            signature += ':' + std::to_string(path->out_index[i]);
        signature += '|';
    }
    char sweep_buf[192];
    std::snprintf(sweep_buf, sizeof(sweep_buf), "%.17g,%.17g,%d,%.17g,%d,%d", m_start_freq,
                  m_stop_freq, m_points, m_stimulus_power_dBm, m_point_a_pin, m_point_b_pin);
    signature += sweep_buf;

    if (signature == m_cached_signature)
        return; // unchanged since last frame -- m_gain_dB/m_nf_dB already hold the answer
    m_cached_signature = std::move(signature);

    m_gain_dB.assign(N, std::numeric_limits<double>::quiet_NaN());
    m_nf_dB.assign(N, std::numeric_limits<double>::quiet_NaN());

    // Private, throwaway clone chain: a fresh scratch graph+registry for this
    // pass only, destroyed at function exit. The real graph/registry are never
    // read for signal purposes and never written to.
    auto scratch = m_host.beginScratchPass();
    if (!scratch)
        return;

    // Chain = path nodes after Point A's node, through Point B's node (the
    // stimulus replaces Point A's signal, so A's own node is not measured).
    std::vector<IComponentEngine *> clones;
    clones.reserve(path->nodes.size() - 1);
    for (size_t i = 1; i < path->nodes.size(); ++i) {
        IComponentEngine *real = path->nodes[i];
        IComponentEngine *clone = scratch->createClone(real->type_name(), real->id());
        if (!clone)
            return; // unknown type -> no data
        clone->deserialize(real->serialize());
        clones.push_back(clone);
    }

    // Wire by directly assigning SignalNode* pointers (no scratch-graph links
    // needed), the same technique RfSimulatorApp::rewireInputs() uses. Each
    // clone's input reads the SPECIFIC output port that node actually used on
    // the real graph (path->out_index[i]) — not hardcoded port 0, which would
    // silently clone the wrong signal for a multi-output component like a PFB
    // Channelizer whose two outputs carry structurally different spectra.
    for (size_t i = 0; i < clones.size(); ++i) {
        auto &inputs = clones[i]->node().inputs;
        if (inputs.empty())
            return; // a chain component must have an input to feed
        if (i == 0) {
            inputs[0] = &m_stimulus;
        } else {
            const int out_port = path->out_index[i];
            auto &prev_outputs = clones[i - 1]->node().outputs;
            if (out_port < 0 || static_cast<size_t>(out_port) >= prev_outputs.size())
                return; // defensive: should never happen, same type = same pin counts
            inputs[0] = &prev_outputs[static_cast<size_t>(out_port)];
        }
        // Mixer clones borrow the real, live LO input (read-only). The current
        // MixerEngine has a single RF input (the LO is the lo_freq_Hz
        // parameter, already copied exactly by deserialize(real->serialize())),
        // so this is a no-op today; it keeps the clone faithful if a mixer
        // model with a separate LO signal input appears.
        if (clones[i]->type_name() == "mixer" && inputs.size() > 1) {
            const auto &real_inputs = path->nodes[i + 1]->node().inputs;
            if (real_inputs.size() > 1)
                inputs[1] = real_inputs[1];
        }
    }

    for (auto *clone : clones)
        clone->update(0.0);

    // Read the response from Point B's OWN output port — resolved directly
    // against the graph's raw output_pin_ids (not IComponentEngine::
    // outputPinId(port), which several multi-output engines, e.g. the PFB
    // Channelizer, do not override — relying on it would silently fall back
    // to port 0 exactly like the bug this function fixes). Point B may be
    // wired to any of its node's output pins (e.g. a PFB Channelizer's OUT2).
    int point_b_port = 0;
    for (const auto &node : m_graph.nodes()) {
        auto it = std::find(node.output_pin_ids.begin(), node.output_pin_ids.end(), m_point_b_pin);
        if (it != node.output_pin_ids.end()) {
            point_b_port = static_cast<int>(std::distance(node.output_pin_ids.begin(), it));
            break;
        }
    }
    auto &final_outputs = clones.back()->node().outputs;
    if (point_b_port < 0 || static_cast<size_t>(point_b_port) >= final_outputs.size())
        return;

    // Match tones by frequency value against the last clone's output, exactly
    // like the prior gain/NF formula (k/T/dbToLinear reused from common.h; no
    // Friis math re-derived). A dropped/mistranslated point degrades to NaN.
    //
    // response->tones/frequencies can carry a few thousand entries once
    // harmonics/IMD tones from a nonlinear stage are appended, and N (the
    // sweep point count) goes up to 2001 -- linearly rescanning both arrays
    // for EVERY stimulus point was O(N*M): measured ~22ms per update() at
    // 2001 points (vs. ~0.03ms at 21), run unconditionally every ImGui frame
    // while the panel is open. This was the "serious performance
    // degradation when enabled" regression. Bucket both arrays once into
    // 1 Hz cells (O(M)) so each of the N lookups below is O(1) amortized;
    // checking a cell plus its two neighbors preserves exact-epsilon
    // matching across cell boundaries, and picking the lowest tone/frequency
    // index within range keeps the same "first match in array order"
    // tie-break as the old linear scan.
    const Spectrum *response = &final_outputs[static_cast<size_t>(point_b_port)];
    constexpr double kFreqEpsilonHz = 1.0;
    const auto cell_of = [](double f) {
        return static_cast<long long>(std::floor(f / kFreqEpsilonHz));
    };

    std::unordered_multimap<long long, size_t> tone_cells;
    tone_cells.reserve(response->tones.size());
    for (size_t t = 0; t < response->tones.size(); ++t)
        tone_cells.emplace(cell_of(response->tones[t].freq_Hz), t);

    std::unordered_multimap<long long, size_t> freq_cells;
    freq_cells.reserve(response->frequencies.size());
    for (size_t j = 0; j < response->frequencies.size(); ++j)
        freq_cells.emplace(cell_of(response->frequencies[j]), j);

    for (size_t i = 0; i < N; ++i) {
        const double f = m_stimulus_freqs[i];
        const long long c = cell_of(f);

        std::optional<size_t> tone_idx;
        for (long long cc = c - 1; cc <= c + 1; ++cc) {
            auto range = tone_cells.equal_range(cc);
            for (auto it = range.first; it != range.second; ++it) {
                if (std::abs(response->tones[it->second].freq_Hz - f) <= kFreqEpsilonHz &&
                    (!tone_idx || it->second < *tone_idx))
                    tone_idx = it->second;
            }
        }
        if (!tone_idx)
            continue;

        const double gain_dB = response->tones[*tone_idx].power_dBm - m_stimulus_power_dBm;
        if (gain_dB < -100.0)
            continue; // indistinguishable from noise floor -> no data

        std::optional<size_t> noise_idx;
        for (long long cc = c - 1; cc <= c + 1; ++cc) {
            auto range = freq_cells.equal_range(cc);
            for (auto it = range.first; it != range.second; ++it) {
                if (std::abs(response->frequencies[it->second] - f) <= kFreqEpsilonHz &&
                    (!noise_idx || it->second < *noise_idx))
                    noise_idx = it->second;
            }
        }

        m_gain_dB[i] = gain_dB;

        if (noise_idx && *noise_idx < response->noise_total_W.size()) {
            const double gain_linear = dbToLinear(gain_dB);
            const double noise_out_W = response->noise_total_W[*noise_idx];
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
