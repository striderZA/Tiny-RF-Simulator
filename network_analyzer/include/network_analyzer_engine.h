#pragma once

#include "spectrum.h"
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class IComponentEngine;
class NodeGraphEngine;

// ---------------------------------------------------------------------------
// App-layer dependency injection (layering resolution, see Task 1 report).
//
// NetworkAnalyzerEngine lives in the DSP-engines layer (network_analyzer/),
// which sits BELOW app/ in the dependency graph. The two lookups the engine
// needs — resolving a graph node to its live engine (ComponentRegistry::find)
// and cloning a component type (ComponentTypeRegistry::find + create) —
// are app-layer concerns. Rather than make a lower layer depend on app/ (a
// CMake/link cycle, since app already links network_analyzer_engine), the app
// layer implements these interfaces and injects them; the engine only ever
// sees IComponentEngine*/NodeGraphEngine, both lower-layer types.
//
// INetworkAnalyzerScratch — one private, throwaway scratch graph+registry per
// measurement pass. Clones created from it are destroyed with it (RAII), so a
// pass never touches the real graph/registry.
// ---------------------------------------------------------------------------
class INetworkAnalyzerScratch {
  public:
    virtual ~INetworkAnalyzerScratch() = default;

    // Construct an engine of the given canonical type (e.g. "attenuator") in
    // the scratch graph, with the given component id. Returns nullptr for an
    // unknown type. The caller applies parameters via deserialize().
    virtual IComponentEngine *createClone(std::string_view type, int id) = 0;
};

class INetworkAnalyzerHost {
  public:
    virtual ~INetworkAnalyzerHost() = default;

    // The live engine owning a graph node (nullptr if none/unregistered).
    virtual IComponentEngine *componentForNode(int graph_node_id) const = 0;

    // A fresh scratch pass for one measurement. Destroyed at the end of the
    // current computeMeasurement() call.
    virtual std::unique_ptr<INetworkAnalyzerScratch> beginScratchPass() const = 0;
};

// Idealized two-port instrument presented as a singleton floating panel (like
// the Spectrum Analyzer). Not an IComponentEngine: no graph node, no pins, no
// ComponentRegistry row. Two probe points (Point A = reference/upstream,
// Point B = measured/downstream) pick real output pins in the graph; the
// instrument walks the real graph's real links forward from A's node to B's
// node, requires exactly one distinct path that never crosses a Combiner's
// combined signal input, and measures that chain on a private, throwaway
// clone chain fed a synthetic tone-comb stimulus ("cheat" mode — the real,
// live simulation and every real component's real state are never read for
// signal purposes and never written to).
//
// v3 replaces the v1/v2 wired-pin engine entirely: no ComponentEngineBase, no
// outputPinId()/inputPinId(), no writing to outputs[0] of a real graph node.
class NetworkAnalyzerEngine {
  public:
    NetworkAnalyzerEngine(NodeGraphEngine &graph, INetworkAnalyzerHost &host);

    void setStartFrequency(double hz);
    void setStopFrequency(double hz);
    void setPoints(int n); // clamped to [2, 2001]
    void setStimulusPower(double dBm);

    // Point A/B are output pin ids — the same identifier space NodeGraphEngine's
    // probe mechanism uses (resolved to a SignalNode+output_index via the same
    // pin lookup); -1 = unset.
    void setPointA(int pin_id);
    void setPointB(int pin_id);
    int pointAPin() const { return m_point_a_pin; }
    int pointBPin() const { return m_point_b_pin; }

    double startFrequency() const { return m_start_freq; }
    double stopFrequency() const { return m_stop_freq; }
    int points() const { return m_points; }
    double stimulusPower() const { return m_stimulus_power_dBm; }

    // Results for the widget. NaN at an index = no path, an ambiguous path, or
    // no matching tone found at the end of the isolated chain.
    const std::vector<double> &sweepFrequencies() const { return m_stimulus_freqs; }
    const std::vector<double> &gainDb() const { return m_gain_dB; }
    const std::vector<double> &noiseFigureDb() const { return m_nf_dB; }

    // Called once per frame from RfSimulatorApp's update loop while the panel
    // is visible. findUniquePath() (a DFS over the live graph) and a cheap
    // signature of the discovered chain (each path node's live serialize()
    // dump + the sweep params) run every frame regardless -- both are O(graph
    // size)/O(chain length), independent of the sweep point count. The
    // expensive part -- cloning the chain and re-running each clone's DSP
    // across up to 2001 points -- is SKIPPED when that signature matches the
    // last recompute (the common case: panel open, nothing being edited);
    // see computeMeasurement()'s dirty-check. A prior version of this
    // comment claimed the full recompute was "microseconds" unconditionally;
    // measurement showed ~22ms/update() at 2001 points before an O(N*M)
    // tone-matching fix, and several ms remained afterward for a chain with
    // a nonlinear stage -- hence the signature-gated skip below.
    void update();

    // Project-level state (this class is not an IComponentEngine).
    nlohmann::json serialize() const;
    void deserialize(const nlohmann::json &);

  private:
    NodeGraphEngine &m_graph;
    INetworkAnalyzerHost &m_host; // resolves live engines + builds clone passes

    double m_start_freq = 1e9;
    double m_stop_freq = 6e9;
    int m_points = 201;
    double m_stimulus_power_dBm = -30.0; // small-signal/linear by default
    int m_point_a_pin = -1;
    int m_point_b_pin = -1;
    bool m_sweep_params_dirty = true;

    std::vector<double> m_stimulus_freqs; // == m_stimulus.frequencies
    std::vector<double> m_gain_dB;
    std::vector<double> m_nf_dB;
    std::string m_cached_signature; // see computeMeasurement()'s dirty-check
    Spectrum m_stimulus; // private tone-comb stimulus, not attached to any node

    void rebuildStimulus();

    // A discovered chain: nodes[0] is Point A's own node (never cloned — its
    // signal is replaced by the stimulus), nodes[1..] are the components to
    // clone and measure, in order, ending at Point B's node. out_index[i] is
    // the output PORT that nodes[i] actually used on the real graph to reach
    // nodes[i+1] (size == nodes.size()-1) — required because some components
    // (e.g. the PFB Channelizer) have multiple, structurally different
    // outputs; hardcoding port 0 would silently clone the wrong signal.
    struct PathResult {
        std::vector<IComponentEngine *> nodes;
        std::vector<int> out_index;
    };

    // DFS over m_graph's real links from Point A's owning node forward,
    // enumerating simple paths to Point B's owning node. Returns nullopt for
    // zero or more-than-one distinct path, or for any path that would cross a
    // Combiner's combined signal input (the start node is exempt: its output
    // is the injection point, replaced by the stimulus). Bail out early once 2
    // distinct paths are found — only "unique or not" matters.
    std::optional<PathResult> findUniquePath() const;
    void computeMeasurement();
};
