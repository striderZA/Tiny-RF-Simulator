#pragma once

#include "measurement_chain_runner.h"
#include "spectrum.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class NodeGraphEngine;

// Idealized two-port instrument presented as a singleton floating panel (like
// the Spectrum Analyzer). Not an IComponentEngine: no graph node, no pins, no
// ComponentRegistry row. Two probe points (Point A = reference/upstream,
// Point B = measured/downstream) pick real output pins in the graph; the
// instrument walks the real graph's real links forward from A's node to B's
// node, requires exactly one distinct path that does not enter a merging
// multi-input component (a combiner). It preserves the exact input and output
// ports used by the path, allowing a switch with only one linked throw input
// to be measured; the chain runs on a private clone fed a synthetic tone-comb
// stimulus ("cheat" mode — the real, live simulation and every real component's
// state are never read for signal purposes and never written to).
//
// v3 replaces the v1/v2 wired-pin engine entirely: no ComponentEngineBase, no
// outputPinId()/inputPinId(), no writing to outputs[0] of a real graph node.
class NetworkAnalyzerEngine {
  public:
    NetworkAnalyzerEngine(const NodeGraphEngine &graph, IMeasurementChainHost &host);

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

    // Called each frame while visible: path discovery and the component-state
    // signature are cheap; the clone-and-cascade is skipped when the path or
    // sweep parameters have not changed.
    void update();

    // Project-level state (this class is not an IComponentEngine).
    nlohmann::json serialize() const;
    void deserialize(const nlohmann::json &);

  private:
    const NodeGraphEngine &m_graph;

    IMeasurementChainHost &m_host; // resolves live engines + builds clone passes

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
    void computeMeasurement();
};
