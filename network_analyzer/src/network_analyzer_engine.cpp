#include "network_analyzer_engine.h"

#include "common.h"
#include "component_interface.h"
#include "node_graph_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

NetworkAnalyzerEngine::NetworkAnalyzerEngine(const NodeGraphEngine &graph,
                                             INetworkAnalyzerHost &host)
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

std::optional<NetworkAnalyzerEngine::MeasurementGraph>
NetworkAnalyzerEngine::findMeasurementGraph() const {
    const int start_node = m_graph.nodeIdForPin(m_point_a_pin);
    const int end_node = m_graph.nodeIdForPin(m_point_b_pin);
    if (start_node < 0 || end_node < 0 || start_node == end_node)
        return std::nullopt;

    // Pin -> (node, port) for both pin directions. Keeping the port lets the
    // clone reproduce the exact wiring through multi-port components instead
    // of assuming port zero.
    struct PinOwner {
        int node;
        int port;
    };
    std::unordered_map<int, PinOwner> input_owner;
    std::unordered_map<int, PinOwner> output_owner;
    for (const auto &node : m_graph.nodes()) {
        for (size_t i = 0; i < node.input_pin_ids.size(); ++i)
            input_owner.emplace(node.input_pin_ids[i], PinOwner{node.node_id, static_cast<int>(i)});
        for (size_t o = 0; o < node.output_pin_ids.size(); ++o)
            output_owner.emplace(node.output_pin_ids[o],
                                 PinOwner{node.node_id, static_cast<int>(o)});
    }

    // Distinct links between known pins. Duplicate links (same start pin ->
    // same end pin, which the graph allows) collapse so they cannot fake a
    // second feed into one input.
    struct Link {
        PinOwner from;
        PinOwner to;
    };
    std::set<std::pair<int, int>> seen_pins;
    std::vector<Link> links;
    for (const auto &link : m_graph.links()) {
        const auto from = output_owner.find(link.start_pin_id);
        const auto to = input_owner.find(link.end_pin_id);
        if (from == output_owner.end() || to == input_owner.end())
            continue;
        if (!seen_pins.emplace(link.start_pin_id, link.end_pin_id).second)
            continue;
        links.push_back({from->second, to->second});
    }

    // Adjacency built once, so the walks and the sort below stay
    // O((V + E) log V) per frame rather than rescanning every link per node.
    std::unordered_map<int, std::vector<const Link *>> out_links;
    std::unordered_map<int, std::vector<const Link *>> in_links;
    for (const auto &link : links) {
        out_links[link.from.node].push_back(&link);
        in_links[link.to.node].push_back(&link);
    }

    // Forward reach from Point A's node. Its outputs are all replaced by the
    // stimulus (the existing contract: Point A selects the reference node,
    // whichever of its outputs the path leaves through), so the walk never
    // re-enters that node and whatever feeds it is irrelevant.
    std::unordered_set<int> forward;
    std::vector<int> stack{start_node};
    while (!stack.empty()) {
        const int node = stack.back();
        stack.pop_back();
        for (const Link *link : out_links[node]) {
            if (link->to.node != start_node && forward.insert(link->to.node).second)
                stack.push_back(link->to.node);
        }
    }
    if (!forward.count(end_node))
        return std::nullopt;

    // Keep only forward-reached nodes that can still reach Point B; dead-end
    // branches (e.g. a splitter output going elsewhere) do not affect B.
    std::unordered_set<int> members{end_node};
    stack.assign(1, end_node);
    while (!stack.empty()) {
        const int node = stack.back();
        stack.pop_back();
        for (const Link *link : in_links[node]) {
            if (forward.count(link->from.node) && members.insert(link->from.node).second)
                stack.push_back(link->from.node);
        }
    }

    // Every input of a member must be reproducible on the clone: fed by
    // Point A's node (the stimulus) or by another member. A feed from
    // neither is a live source the private clone cannot represent, so reject
    // it rather than report a plausible but wrong NF.
    for (int node_id : members) {
        for (const Link *link : in_links[node_id]) {
            if (link->from.node != start_node && !members.count(link->from.node))
                return std::nullopt;
        }
    }

    std::unordered_map<int, IComponentEngine *> component_of;
    for (int node_id : members) {
        auto *comp = m_host.componentForNode(node_id);
        if (!comp)
            return std::nullopt; // unregistered node — cannot clone it
        component_of.emplace(node_id, comp);

        const auto &node_feeds = in_links[node_id];
        std::unordered_set<int> fed_ports;
        for (const Link *link : node_feeds) {
            if (!fed_ports.insert(link->to.port).second)
                return std::nullopt; // two links into one input pin
        }
        // Only the 2:1 switch defines how two inputs combine (selected throw
        // at insertion loss, the other at isolation). Any other multi-input
        // component, such as a combiner, stays unsupported even when singly
        // fed.
        if (comp->numInputPins() > 1 && comp->type_name() != "rf_switch_spdt_2to1")
            return std::nullopt;
    }
    auto *start_comp = m_host.componentForNode(start_node);
    if (!start_comp)
        return std::nullopt;

    // Kahn topological sort; ties break by graph node order so the result is
    // deterministic. A leftover member means a cycle, which a single
    // feed-forward clone pass cannot evaluate. Stimulus feeds from Point A's
    // node are already available and do not count as pending inputs.
    std::unordered_map<int, size_t> graph_order;
    for (const auto &node : m_graph.nodes())
        graph_order.emplace(node.node_id, graph_order.size());
    std::unordered_map<int, int> pending_inputs;
    std::set<std::pair<size_t, int>> ready;
    for (int node_id : members) {
        int pending = 0;
        for (const Link *link : in_links[node_id])
            pending += link->from.node != start_node ? 1 : 0;
        pending_inputs[node_id] = pending;
        if (pending == 0)
            ready.emplace(graph_order.at(node_id), node_id);
    }

    MeasurementGraph result;
    result.nodes.push_back(start_comp);
    std::unordered_map<int, size_t> index_of{{start_node, 0}};
    while (!ready.empty()) {
        const int node_id = ready.begin()->second;
        ready.erase(ready.begin());
        index_of.emplace(node_id, result.nodes.size());
        result.nodes.push_back(component_of.at(node_id));
        for (const Link *link : out_links[node_id]) {
            if (members.count(link->to.node) && --pending_inputs[link->to.node] == 0)
                ready.emplace(graph_order.at(link->to.node), link->to.node);
        }
    }
    if (index_of.size() != members.size() + 1)
        return std::nullopt; // cycle

    // Every other member is an ancestor of Point B's node, so B is the only
    // sink and Kahn's order places it last. Check rather than assume: the
    // caller reads Point B's response from nodes.back().
    if (index_of.at(end_node) != result.nodes.size() - 1)
        return std::nullopt;

    for (int node_id : members) {
        for (const Link *link : in_links[node_id])
            result.edges.push_back({index_of.at(link->from.node), link->from.port,
                                    index_of.at(node_id), link->to.port});
    }
    std::sort(result.edges.begin(), result.edges.end(), [](const auto &a, const auto &b) {
        return std::tie(a.to, a.in_port, a.from, a.out_port) <
               std::tie(b.to, b.in_port, b.from, b.out_port);
    });
    return result;
}

void NetworkAnalyzerEngine::computeMeasurement() {
    const size_t N = m_stimulus_freqs.size();

    auto graph = findMeasurementGraph();
    // No path, unsupported topology, or Point A == Point B -> all-NaN. The
    // subgraph must contain at least one component: the stimulus is injected
    // at Point A's output, which replaces the start node's own output.
    if (!graph || graph->nodes.size() < 2) {
        m_gain_dB.assign(N, std::numeric_limits<double>::quiet_NaN());
        m_nf_dB.assign(N, std::numeric_limits<double>::quiet_NaN());
        m_cached_signature.clear();
        return;
    }

    // Dirty-check: everything below here re-runs each member component's
    // full DSP across up to 2001 points -- real, non-trivial work (a nonlinear
    // stage's harmonics/IMD generation scales with tone count) -- and this
    // function runs unconditionally every ImGui frame the panel is visible.
    // This instrument has no wired input to compare a cached pointer +
    // generation against (every other engine's dirty-check), since it reads
    // REAL, externally-owned components it gets no change notification from.
    // Stand in with a signature of the discovered subgraph (each node's live
    // serialize() dump -- %.17g-precise doubles round-trip exactly, so this
    // only changes when a value actually changes -- and every edge) plus the
    // sweep params and both probe pins; skip the clone-and-cascade below,
    // reusing last frame's m_gain_dB/m_nf_dB, when nothing in it moved.
    std::string signature;
    signature.reserve(256);
    for (const auto *node : graph->nodes) {
        signature += node->type_name();
        signature += ':';
        signature += std::to_string(node->id());
        signature += node->serialize().dump();
        signature += '|';
    }
    for (const auto &edge : graph->edges) {
        signature += std::to_string(edge.from) + '.' + std::to_string(edge.out_port) + '>' +
                     std::to_string(edge.to) + '.' + std::to_string(edge.in_port) + ';';
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

    // Private, throwaway clones: a fresh scratch graph+registry for this pass
    // only, destroyed at function exit. The real graph/registry are never
    // read for signal purposes and never written to.
    auto scratch = m_host.beginScratchPass();
    if (!scratch)
        return;

    // Clone every member after Point A's node (the stimulus replaces Point
    // A's signal, so A's own node is not measured). clones[i] mirrors
    // graph->nodes[i]; clones[0] stays null.
    std::vector<IComponentEngine *> clones(graph->nodes.size(), nullptr);
    for (size_t i = 1; i < graph->nodes.size(); ++i) {
        IComponentEngine *real = graph->nodes[i];
        IComponentEngine *clone = scratch->createClone(real->type_name(), real->id());
        if (!clone)
            return; // unknown type -> no data
        clone->deserialize(real->serialize());
        clones[i] = clone;
    }

    // Wire by directly assigning SignalNode* pointers (no scratch-graph links
    // needed), the same technique RfSimulatorApp::rewireInputs() uses. Each
    // edge carries the exact output and input ports of the live link, which
    // multi-output components and both RF switch throws rely on. An input
    // with no edge stays null, exactly as an unlinked live input does.
    for (const auto &edge : graph->edges) {
        auto &inputs = clones[edge.to]->node().inputs;
        const size_t input_port = static_cast<size_t>(edge.in_port);
        if (input_port >= inputs.size())
            return; // defensive: clone pin counts must match the live engine
        if (edge.from == 0) {
            inputs[input_port] = &m_stimulus;
            continue;
        }
        auto &prev_outputs = clones[edge.from]->node().outputs;
        if (edge.out_port < 0 || static_cast<size_t>(edge.out_port) >= prev_outputs.size())
            return; // defensive: should never happen, same type = same pin counts
        inputs[input_port] = &prev_outputs[static_cast<size_t>(edge.out_port)];
    }

    // nodes[] is topologically ordered, so every clone's inputs are final
    // before it runs.
    for (size_t i = 1; i < clones.size(); ++i)
        clones[i]->update(0.0);

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

    // Match tones by frequency value against Point B's clone output, exactly
    // like the prior gain/NF formula (k/T/dbToLinear reused from common.h; no
    // Friis math re-derived). A dropped/mistranslated point degrades to NaN.
    //
    // Gain is the FIRST tone at each sweep frequency; same-frequency tones
    // are deliberately not power-summed, because harmonics/IMD from a
    // nonlinear stage can land on another sweep point. A 2:1 switch emits its
    // selected throw's tones before the unselected throw's leakage, so Gain
    // through a switched filter bank is the selected-throw path, while the
    // NF numerator (total noise per bin) includes both throws.
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
