#pragma once

#include "common.h"
#include "node_graph_engine.h"
#include "signal_node.h"
#include "spectrum.h"
#include <cmath>
#include <complex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct PFBChannel {
    int channel_index;
    double center_freq_Hz;
    double bandwidth_Hz;

    std::vector<int> bin_indices;
    std::vector<double> bin_weights;
    double noise_W = 0.0;
    std::vector<Spectrum::Tone> tones;
};

struct PFBConfig {
    int M = 32;
    int K = 8;
    double Fs_Hz = 0.0;
    double beta = 8.0;
};

class PFBChannelizerEngine {
  public:
    PFBChannelizerEngine(int id, NodeGraphEngine& graph);

    int id() const { return m_id; }
    int graphNodeId() const { return m_graph_node_id; }
    std::string hoverSummary() const;
    int inputPinId() const;
    int outputPinId() const;
    void serialize(nlohmann::json& j) const;
    void deserialize(const nlohmann::json& j);
    bool isDirty() const { return m_dirty; }

    void setChannelCount(int M);
    void setTapsPerBranch(int K);
    void setKaiserBeta(double beta);
    void setActiveChannel(int ch);
    void setFs_Hz(double fs) {
        if (fs != m_cfg.Fs_Hz) { m_cfg.Fs_Hz = fs; m_dirty = true; }
    }

    int channelCount() const { return m_cfg.M; }
    int tapsPerBranch() const { return m_cfg.K; }
    double kaiserBeta() const { return m_cfg.beta; }
    int activeChannel() const { return m_active_channel; }
    double fs_Hz() const { return m_cfg.Fs_Hz; }
    const std::vector<PFBChannel>& channels() const { return m_channels; }

    double activeChannelCenter_Hz() const {
        return m_active_channel >= 0 && m_active_channel < static_cast<int>(m_channels.size())
            ? m_channels[m_active_channel].center_freq_Hz : 0.0;
    }
    double activeChannelBandwidth_Hz() const {
        return m_active_channel >= 0 && m_active_channel < static_cast<int>(m_channels.size())
            ? m_channels[m_active_channel].bandwidth_Hz : 0.0;
    }

    void update(double dt);
    SignalNode& node() { return m_node; }

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;
    SignalNode m_node;

    PFBConfig m_cfg;
    int m_active_channel = 0;
    std::vector<PFBChannel> m_channels;
    bool m_dirty = true;
    const Spectrum* m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;
    std::vector<double> m_cached_freqs;
    double m_cached_Fs_Hz = 0;

    void recomputeChannels(const std::vector<double>& freqs);
    double prototypeResponse(double offset_Hz) const;
    double kaiserWindow(double x) const;
};
