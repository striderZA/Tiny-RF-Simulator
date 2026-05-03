#define _USE_MATH_DEFINES
#include "iq_plot_widget.h"
#include "common.h"
#include "imgui.h"
#include "implot.h"
#include <kiss_fft.h>
#include <cmath>
#include <complex>

void IQPlotWidget::draw(const char* title, bool* p_open) {
    ImGui::SetNextWindowSize(ImVec2(500, 350), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    if (m_pfb.channels().empty()) {
        ImGui::Text("No PFB channels available");
        ImGui::End();
        return;
    }

    const auto& ch = m_pfb.channels()[m_pfb.activeChannel()];
    const auto& out = m_pfb.node().outputs[0];

    size_t N = ch.bin_indices.size();
    if (N < 2) {
        ImGui::Text("Channel has too few bins for IDFT");
        ImGui::End();
        return;
    }

    double Fs = m_pfb.fs_Hz();
    int M = m_pfb.channelCount();
    double time_step_s = static_cast<double>(M) / (2.0 * Fs);

    // Build complex spectrum for IDFT from weighted noise + tones
    std::vector<std::complex<double>> spectrum(N, {0.0, 0.0});
    double bin_width = (N > 1) ? (out.frequencies.back() - out.frequencies.front()) / static_cast<double>(N - 1) : 1.0;

    for (size_t i = 0; i < N; ++i) {
        double psd = (i < out.noise_total_W.size()) ? out.noise_total_W[i] : 0.0;
        double magnitude = std::sqrt(std::max(0.0, psd * bin_width));
        double phase_rad = (i < out.phase_deg.size()) ? out.phase_deg[i] * M_PI / 180.0 : 0.0;
        spectrum[i] = std::complex<double>(magnitude * std::cos(phase_rad), magnitude * std::sin(phase_rad));
    }

    // Add tones as complex sinusoids
    for (const auto& tone : out.tones) {
        // Find the bin closest to this tone's frequency
        double best_dist = std::numeric_limits<double>::max();
        int best_idx = -1;
        for (size_t i = 0; i < N; ++i) {
            double dist = std::abs(out.frequencies[i] - tone.freq_Hz);
            if (dist < best_dist) {
                best_dist = dist;
                best_idx = static_cast<int>(i);
            }
        }
        if (best_idx >= 0 && best_idx < static_cast<int>(N)) {
            double mag = std::pow(10.0, tone.power_dBm / 20.0) / std::sqrt(50.0);
            double phase_rad = tone.phase_deg * M_PI / 180.0;
            spectrum[best_idx] += std::complex<double>(mag * std::cos(phase_rad), mag * std::sin(phase_rad));
        }
    }

    // IDFT using kissFFT (inverse transform)
    std::vector<double> time_domain_i(N, 0.0);
    std::vector<double> time_domain_q(N, 0.0);
    {
        std::vector<kiss_fft_cpx> fd(N);
        std::vector<kiss_fft_cpx> td(N);

        for (size_t i = 0; i < N; ++i) {
            fd[i].r = static_cast<float>(spectrum[i].real());
            fd[i].i = static_cast<float>(spectrum[i].imag());
        }

        kiss_fft_cfg ifft = kiss_fft_alloc(static_cast<int>(N), 1, nullptr, nullptr);

        kiss_fft(ifft, fd.data(), td.data());

        for (size_t i = 0; i < N; ++i) {
            time_domain_i[i] = static_cast<double>(td[i].r) / static_cast<double>(N);
            time_domain_q[i] = static_cast<double>(td[i].i) / static_cast<double>(N);
        }

        kiss_fft_free(ifft);
    }

    // Build time axis (microseconds)
    m_time_us.resize(N);
    m_i_samples.resize(N);
    m_q_samples.resize(N);
    for (size_t i = 0; i < N; ++i) {
        m_time_us[i] = static_cast<double>(i) * time_step_s * 1e6;
        m_i_samples[i] = time_domain_i[i];
        m_q_samples[i] = time_domain_q[i];
    }

    int M_i = m_pfb.channelCount();
    int K_i = m_pfb.tapsPerBranch();
    double beta_i = m_pfb.kaiserBeta();

    ImGui::Text("Ch %d/%d | M=%d K=%d B=%.1f | Fs=%.0f MHz | dt=%.2f ns",
        m_pfb.activeChannel(), m_pfb.channelCount(), M_i, K_i, beta_i,
        m_pfb.fs_Hz() / 1e6, time_step_s * 1e9);

    if (ImPlot::BeginPlot("##IQ")) {
        ImPlot::SetupAxes("Time (us)", "Amplitude");
        ImPlot::PlotLine("I", m_time_us.data(), m_i_samples.data(), static_cast<int>(N));
        ImPlot::PlotLine("Q", m_time_us.data(), m_q_samples.data(), static_cast<int>(N));
        ImPlot::EndPlot();
    }

    ImGui::End();
}
