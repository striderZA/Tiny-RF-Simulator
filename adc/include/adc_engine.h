#pragma once

#include <complex>
#include <vector>

#include "iq_stream.h"
#include "node_graph_engine.h"
#include "signal_node.h"

class AdcEngine {
public:
    AdcEngine(int id, NodeGraphEngine& graph);

    int id() const { return m_id; }
    int graphNodeId() const { return m_graph_node_id; }
    int inputPinId() const;
    int outputPinId() const;

    void update(double dt);

    const IQStream& iqOutput() const { return m_iq_output; }

    double fs_Hz() const { return m_fs_Hz; }
    void setFs_Hz(double fs) { m_fs_Hz = fs; m_dirty = true; }
    double nsd_dBm_per_Hz() const { return m_nsd_dBm_per_Hz; }
    void setNsd_dBm_per_Hz(double nsd) { m_nsd_dBm_per_Hz = nsd; m_dirty = true; }
    int bits() const { return m_bits; }
    void setBits(int b) { m_bits = b > 0 ? b : 1; m_dirty = true; }
    double v_fs() const { return m_v_fs; }
    void setVfs(double v) { m_v_fs = v; m_dirty = true; }
    double fChannel_Hz() const { return m_f_channel_Hz; }
    void setFChannel_Hz(double fc) { m_f_channel_Hz = fc; m_dirty = true; }
    int decimation() const { return m_decim; }
    void setDecimation(int d) { m_decim = d > 0 ? d : 1; m_dirty = true; }
    int nSamples() const { return m_n_samples; }
    void setNSamples(int n) { m_n_samples = n; m_dirty = true; }

    bool loaded() const { return true; }

    SignalNode& node() { return m_node; }

private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph;
    SignalNode m_node;

    double m_fs_Hz = 1e9;
    double m_nsd_dBm_per_Hz = -155.0;
    int m_bits = 12;
    double m_v_fs = 2.0;
    double m_f_channel_Hz = 2.4e9;
    int m_decim = 50;
    int m_n_samples = 16384;

    IQStream m_iq_output;
    bool m_dirty = true;

    std::vector<Spectrum::Tone> m_last_tones;

    std::vector<double> m_lpf_coeffs;
    double m_lpf_cached_fs = 0.0;
    int m_lpf_cached_decim = 0;
};
