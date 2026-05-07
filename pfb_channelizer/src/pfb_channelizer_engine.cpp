#define _USE_MATH_DEFINES
#include "pfb_channelizer_engine.h"
#include <algorithm>
#include <cmath>
#include <limits>

PFBChannelizerEngine::PFBChannelizerEngine(int id, NodeGraphEngine& graph)
    : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("PFB " + std::to_string(id), &m_node, 1, 2);
    m_node.inputs.resize(1);
    m_node.outputs.resize(2);
}

int PFBChannelizerEngine::inputPinId() const {
    return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1;
}

int PFBChannelizerEngine::outputPinId() const {
    return m_graph ? m_graph->outputPinId(m_graph_node_id) : -1;
}

void PFBChannelizerEngine::setChannelCount(int M) {
    if (M < 2) M = 2;
    if (M > 1024) M = 1024;
    if (M != m_cfg.M) { m_cfg.M = M; m_dirty = true; }
}

void PFBChannelizerEngine::setTapsPerBranch(int K) {
    if (K < 1) K = 1;
    if (K > 64) K = 64;
    if (K != m_cfg.K) { m_cfg.K = K; m_dirty = true; }
}

void PFBChannelizerEngine::setKaiserBeta(double beta) {
    if (beta < 0.0) beta = 0.0;
    if (beta > 20.0) beta = 20.0;
    if (beta != m_cfg.beta) { m_cfg.beta = beta; m_dirty = true; }
}

void PFBChannelizerEngine::setActiveChannel(int ch) {
    if (ch < 0) ch = 0;
    if (ch >= m_cfg.M) ch = m_cfg.M - 1;
    if (ch != m_active_channel) { m_active_channel = ch; m_dirty = true; }
}

void PFBChannelizerEngine::update(double) {
    const Spectrum* in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
    if (!m_dirty && in_ptr == m_cached_input_ptr && (!in_ptr || in_ptr->generation == m_cached_input_generation))
        return;
    m_dirty = false;

    if (in_ptr && in_ptr->fs_Hz > 0.0) {
        m_cfg.Fs_Hz = in_ptr->fs_Hz;
    }

    m_cached_input_ptr = in_ptr;
    if (in_ptr)
        m_cached_input_generation = in_ptr->generation;

    auto& out = m_node.outputs[0];

    if (!in_ptr || in_ptr->frequencies.size() < 2 || m_cfg.Fs_Hz <= 0.0) {
        out.frequencies.clear();
        out.tones.clear();
        out.noise_W.clear();
        out.noise_total_W.clear();
        if (!out.frequencies.empty()) out.bumpGeneration();
        return;
    }

    // Only recompute channels when frequency grid or Fs changes
    if (in_ptr->frequencies != m_cached_freqs || m_cfg.Fs_Hz != m_cached_Fs_Hz) {
        recomputeChannels(in_ptr->frequencies);
        m_cached_freqs = in_ptr->frequencies;
        m_cached_Fs_Hz = m_cfg.Fs_Hz;
    }

    double bin_width = (in_ptr->frequencies.size() > 1)
        ? in_ptr->frequencies[1] - in_ptr->frequencies[0] : 1.0;

    for (auto& ch : m_channels) {
        ch.noise_W = 0.0;
        ch.tones.clear();

        for (size_t i = 0; i < ch.bin_indices.size(); ++i) {
            int idx = ch.bin_indices[i];
            double weight = ch.bin_weights[i];

            double psd = (idx < static_cast<int>(in_ptr->noise_total_W.size()))
                ? in_ptr->noise_total_W[idx] : 0.0;
            ch.noise_W += psd * weight * weight * bin_width;
        }

        for (const auto& tone : in_ptr->tones) {
            double offset = tone.freq_Hz - ch.center_freq_Hz;
            if (std::abs(offset) <= ch.bandwidth_Hz) {
                Spectrum::Tone t = tone;
                double w = prototypeResponse(offset);
                double power_lin = std::pow(10.0, tone.power_dBm / 10.0) * w * w;
                t.power_dBm = 10.0 * std::log10(power_lin + 1e-300);
                ch.tones.push_back(t);
            }
        }
    }

    const auto& active = m_channels[m_active_channel];
    size_t n = active.bin_indices.size();
    out.frequencies.resize(n);
    out.noise_W.resize(n, 0.0);
    out.noise_total_W.resize(n, 0.0);
    out.phase_deg.resize(n, 0.0);
    out.tones = active.tones;

    for (size_t i = 0; i < n; ++i) {
        int src_idx = active.bin_indices[i];
        double weight = active.bin_weights[i];
        out.frequencies[i] = in_ptr->frequencies[src_idx];
        double psd = (src_idx < static_cast<int>(in_ptr->noise_total_W.size()))
            ? in_ptr->noise_total_W[src_idx] : 0.0;
        double weighted_psd = psd * weight * weight;
        out.noise_W[i] = weighted_psd;
        out.noise_total_W[i] = weighted_psd;
        if (src_idx < static_cast<int>(in_ptr->phase_deg.size()))
            out.phase_deg[i] = in_ptr->phase_deg[src_idx];
    }

    out.bumpGeneration();

    // Build full spectrum into outputs[1]
    // Each bin gets noise = input PSD * weight² from its channel.
    // The prototype filter weight (< 1.0 at channel edges) reduces noise per bin,
    // making the PFB output noise floor lower than the input — the binning effect.
    auto& out_full = m_node.outputs[1];
    out_full.frequencies = in_ptr->frequencies;

    size_t n_full = in_ptr->frequencies.size();
    out_full.noise_W.assign(n_full, 0.0);
    out_full.noise_total_W.assign(n_full, 0.0);
    out_full.noise_added_W.assign(n_full, 0.0);
    for (const auto& ch : m_channels) {
        for (size_t i = 0; i < ch.bin_indices.size(); ++i) {
            int bin_idx = ch.bin_indices[i];
            double weight = ch.bin_weights[i];
            if (bin_idx >= 0 && bin_idx < static_cast<int>(n_full)) {
                double psd = (bin_idx < static_cast<int>(in_ptr->noise_total_W.size()))
                    ? in_ptr->noise_total_W[bin_idx] : 0.0;
                double weighted = psd * weight * weight;
                out_full.noise_W[bin_idx] += weighted;
                out_full.noise_total_W[bin_idx] += weighted;
            }
        }
    }
    out_full.phase_deg = in_ptr->phase_deg;

    // Collect tones from all channels (channel-weighted)
    out_full.tones.clear();
    for (const auto& ch : m_channels) {
        out_full.tones.insert(out_full.tones.end(), ch.tones.begin(), ch.tones.end());
    }

    out_full.bumpGeneration();
}

void PFBChannelizerEngine::recomputeChannels(const std::vector<double>& freqs) {
    double channel_bw = m_cfg.Fs_Hz / m_cfg.M;
    double nyquist = m_cfg.Fs_Hz / 2.0;
    m_channels.resize(m_cfg.M);

    for (int k = 0; k < m_cfg.M; ++k) {
        auto& ch = m_channels[k];
        ch.channel_index = k;
        ch.center_freq_Hz = -nyquist + channel_bw / 2.0 + k * channel_bw;
        ch.bandwidth_Hz = channel_bw;
        ch.bin_indices.clear();
        ch.bin_weights.clear();

        for (int i = 0; i < static_cast<int>(freqs.size()); ++i) {
            if (freqs[i] < -nyquist || freqs[i] > nyquist)
                continue;
            double offset = freqs[i] - ch.center_freq_Hz;
            if (std::abs(offset) <= channel_bw) {
                ch.bin_indices.push_back(i);
                ch.bin_weights.push_back(prototypeResponse(offset));
            }
        }
    }

}

double PFBChannelizerEngine::prototypeResponse(double offset_Hz) const {
    double x = offset_Hz / (m_cfg.Fs_Hz / m_cfg.M);
    double arg = m_cfg.K * M_PI * x;
    double sinc = (std::abs(arg) < 1e-12) ? 1.0 : std::sin(arg) / arg;
    return std::abs(kaiserWindow(x) * sinc);
}

double PFBChannelizerEngine::kaiserWindow(double x) const {
    double r = 2.0 * x / m_cfg.K;
    if (std::abs(r) > 1.0) return 0.0;
    double arg = 1.0 - r * r;
    return std::cyl_bessel_i(0, m_cfg.beta * std::sqrt(arg))
         / std::cyl_bessel_i(0, m_cfg.beta);
}

std::string PFBChannelizerEngine::hoverSummary() const {
    return "PFB M=" + std::to_string(m_cfg.M) + " K=" + std::to_string(m_cfg.K)
        + " Ch=" + std::to_string(m_active_channel);
}
