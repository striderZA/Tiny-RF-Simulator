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
// Point B = measured/downstream) are stored as output pin ids, but they are
// used asymmetrically: Point A selects a source NODE (every output of that
// node is replaced by the stimulus), while Point B selects the exact output
// PORT read as the response. The instrument collects every component on a
// forward path from Point A's node to Point B's node and runs that subgraph
// on a private clone fed a synthetic tone-comb stimulus ("cheat" mode — the
// real, live simulation and every real component's state are never read for
// signal purposes and never written to). Branches may only rejoin at an RF
// SPDT 2:1 switch, so a switched filter bank (1:2 switch -> parallel branches
// -> 2:1 switch) is measured with both branches present, using that engine's
// model: the selected throw at insertion loss and the other at isolation,
// with their tones concatenated, not coherently summed. Combiners, fan-in
// onto one input pin, and any input fed by neither Point A's node nor
// another subgraph member (e.g. a second live source) are rejected as
// no-data.
//
// Gain_dB at a sweep point is the FIRST output tone at that frequency (see
// computeMeasurement()), and NF_dB = 10*log10(P_noise,out / (G_linear * kT))
// with G_linear = 10^(Gain_dB/10), so NF inherits Gain's tone choice. At a 2:1
// switch the selected throw's tones come first, so Gain is the selected-throw
// path; the unselected throw's same-frequency leakage tone is not
// power-summed into it, although its noise is in the NF numerator. When both
// throws carry comparable power at a frequency (e.g. a filter bank whose two
// switches select different branches), Gain therefore reads low and NF high
// by up to the omitted tone's share.
//
// v3 replaces the v1/v2 wired-pin engine entirely: no ComponentEngineBase, no
// outputPinId()/inputPinId(), no writing to outputs[0] of a real graph node.
class NetworkAnalyzerEngine {
  public:
    NetworkAnalyzerEngine(const NodeGraphEngine &graph, INetworkAnalyzerHost &host);

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

    // Results for the widget. NaN at an index = no path, an unsupported
    // topology, or no matching tone found at Point B on the isolated clone.
    const std::vector<double> &sweepFrequencies() const { return m_stimulus_freqs; }
    const std::vector<double> &gainDb() const { return m_gain_dB; }
    const std::vector<double> &noiseFigureDb() const { return m_nf_dB; }

    // Called once per frame from RfSimulatorApp's update loop while the panel
    // is visible. findMeasurementGraph() (O((V + E) log V) over the live
    // graph) and a cheap signature of the discovered subgraph (each node's
    // live serialize() dump, its links, and the sweep params) run every frame
    // regardless, independent of the sweep point count. The expensive part
    // -- cloning the subgraph and re-running each clone's DSP
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
    const NodeGraphEngine &m_graph;
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
    Spectrum m_stimulus;            // private tone-comb stimulus, not attached to any node

    void rebuildStimulus();

    // The components between Point A and Point B. nodes[0] is Point A's own
    // node (never cloned — its output is replaced by the stimulus); nodes[1..]
    // are the components to clone, in topological order, and nodes.back() is
    // Point B's node. Each edge feeds input port `in_port` of nodes[to] from
    // output port `out_port` of nodes[from] (from == 0 means the stimulus).
    // Preserving both ports matters for multi-port components such as PFB
    // outputs and RF switch throws.
    struct MeasurementGraph {
        struct Edge {
            size_t from;
            int out_port;
            size_t to;
            int in_port;
        };
        std::vector<IComponentEngine *> nodes;
        std::vector<Edge> edges;
    };

    // Collects every node reachable forward from Point A's node (never
    // re-entering it) that can also reach Point B's node. Returns nullopt
    // when Point B is unreachable, the subgraph has a cycle or an
    // unregistered node, a member input is fed by neither Point A's node nor
    // another member, one input pin has more than one link, or a member has
    // several inputs and is not an RF SPDT 2:1 switch (a combiner merges
    // paths with semantics the analyzer does not define).
    std::optional<MeasurementGraph> findMeasurementGraph() const;
    void computeMeasurement();
};
