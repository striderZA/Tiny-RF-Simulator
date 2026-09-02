#pragma once

#include "component_engine_base.h"
#include "node_graph_engine.h"
#include "pfb_filter_design.h"
#include "signal_node.h"
#include "spectrum.h"
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

class PFBChannelizerEngine : public ComponentEngineBase {
  public:
    PFBChannelizerEngine(int id, NodeGraphEngine &graph);

    std::string_view type_name() const override { return "pfb"; }
    std::string hoverSummary() const override;

    void setChannelCount(int M);
    void setTapsPerBranch(int K);
    void setKaiserBeta(double beta);
    void setActiveChannel(int ch);
    void setFs_Hz(double fs) {
        if (fs != m_cfg.Fs_Hz || m_fs_from_input) {
            m_cfg.Fs_Hz = fs;
            m_fs_from_input = false;
            m_dirty = true;
        }
    }

    int channelCount() const { return m_cfg.M; }
    int tapsPerBranch() const { return m_cfg.K; }
    double kaiserBeta() const { return m_cfg.beta; }
    int activeChannel() const { return m_active_channel; }
    double fs_Hz() const { return m_cfg.Fs_Hz; }
    const std::vector<PFBChannel> &channels() const { return m_channels; }

    double activeChannelCenter_Hz() const {
        return m_active_channel >= 0 && m_active_channel < static_cast<int>(m_channels.size())
                   ? m_channels[m_active_channel].center_freq_Hz
                   : 0.0;
    }
    double activeChannelBandwidth_Hz() const {
        return m_active_channel >= 0 && m_active_channel < static_cast<int>(m_channels.size())
                   ? m_channels[m_active_channel].bandwidth_Hz
                   : 0.0;
    }

    void update(double dt) override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json &) override;

  private:
    PFBConfig m_cfg;
    int m_active_channel = 0;
    std::vector<PFBChannel> m_channels;
    std::vector<double> m_cached_freqs;
    double m_cached_Fs_Hz = 0;
    int m_cached_K = 0;
    double m_cached_beta = 0.0;
    bool m_fs_from_input = false;

    void recomputeChannels(const std::vector<double> &freqs);
    // Shared real prototype; rebuilt whenever M/K/beta change. The single
    // source of truth for channel weights (also used by the Filter Calculator).
    PfbFilterDesign m_design{32, 8, 8.0};
};
