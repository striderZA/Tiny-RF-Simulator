#pragma once
#include "component_engine_base.h"
#include "node_graph_engine.h"
#include "signal_node.h"
#include "spectrum.h"
#include <string>
#include <vector>

// Idealized two-port instrument: Port 1 (output pin) emits a calibrated
// tone-comb stimulus across [startFrequency, stopFrequency]; Port 2 (input
// pin) is a read-only measurement tap wired from wherever downstream the
// user wants to measure. Computes forward gain (dB) and noise figure (dB)
// per swept frequency by comparing the known stimulus against whatever
// tone/noise actually arrives at Port 2. Never writes to its own input, so
// wiring Port 2 off an existing signal (ordinary multi-fanout) has zero
// effect on the rest of the circuit.
class NetworkAnalyzerEngine : public ComponentEngineBase {
  public:
    NetworkAnalyzerEngine(int id, NodeGraphEngine &graph);

    std::string_view type_name() const override { return "network_analyzer"; }
    std::string hoverSummary() const override;

    void setStartFrequency(double hz);
    void setStopFrequency(double hz);
    void setPoints(int n); // clamped to [2, 2001]
    void setStimulusPower(double dBm);

    double startFrequency() const { return m_start_freq; }
    double stopFrequency() const { return m_stop_freq; }
    int points() const { return m_points; }
    double stimulusPower() const { return m_stimulus_power_dBm; }

    // Results for the widget. NaN at an index means no matching tone was
    // found at Port 2 (the DUT dropped or frequency-translated that point).
    const std::vector<double> &sweepFrequencies() const { return m_stimulus_freqs; }
    const std::vector<double> &gainDb() const { return m_gain_dB; }
    const std::vector<double> &noiseFigureDb() const { return m_nf_dB; }

    // Test-only: counts actual computeMeasurement() runs, proving the
    // dirty-flag skip path is exercised (not just that results are correct).
    int measurementRecomputeCount() const { return m_measurement_recompute_count; }

    void update(double dt) override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json &) override;

  private:
    double m_start_freq = 1e9;
    double m_stop_freq = 6e9;
    int m_points = 201;
    double m_stimulus_power_dBm = -30.0; // small-signal/linear by default
    bool m_sweep_params_dirty = true;

    std::vector<double> m_stimulus_freqs; // == outputs[0].frequencies
    std::vector<double> m_gain_dB;
    std::vector<double> m_nf_dB;
    int m_measurement_recompute_count = 0;

    void rebuildStimulus();
    void computeMeasurement(const Spectrum *response);
};
