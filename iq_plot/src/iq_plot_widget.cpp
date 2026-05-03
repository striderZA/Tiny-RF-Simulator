#define _USE_MATH_DEFINES
#include "iq_plot_widget.h"
#include "common.h"
#include "imgui.h"
#include "implot.h"
#include <kiss_fft.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

void IQPlotWidget::draw(const char* title, bool* p_open) {
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    if (m_pfb.channels().empty()) {
        ImGui::Text("No PFB channels available");
        ImGui::End();
        return;
    }

    double Fs = m_pfb.fs_Hz();
    int M = m_pfb.channelCount();

    if (!m_time_inited) {
        m_time_step_s = static_cast<double>(M) / (2.0 * Fs);
        m_time_inited = true;
    }

    runIDFT();

    if (m_stream_i.size() < 2) {
        ImGui::Text("Buffering I/Q samples...");
        ImGui::End();
        return;
    }

    // Build plot data from rolling buffer
    size_t n = m_stream_i.size();
    double t0 = m_time_cursor_s - static_cast<double>(n) * m_time_step_s;

    std::vector<double> time_us(n);
    std::vector<double> i_vals(m_stream_i.begin(), m_stream_i.end());
    std::vector<double> q_vals(m_stream_q.begin(), m_stream_q.end());
    for (size_t i = 0; i < n; ++i)
        time_us[i] = (t0 + static_cast<double>(i) * m_time_step_s) * 1e6;

    ImGui::Text("Ch %d/%d | Samp rate: %.2f ksps | Buffer: %zu samp (%.1f us)",
        m_pfb.activeChannel(), m_pfb.channelCount(),
        1e-3 / m_time_step_s, n,
        static_cast<double>(n) * m_time_step_s * 1e6);

    // Auto-scale y-axis to data
    double y_min = *std::min_element(i_vals.begin(), i_vals.end());
    double y_max = *std::max_element(i_vals.begin(), i_vals.end());
    for (double v : q_vals) {
        y_min = std::min(y_min, v);
        y_max = std::max(y_max, v);
    }
    double y_margin = (y_max - y_min) * 0.1 + 1e-12;
    y_min -= y_margin;
    y_max += y_margin;

    ImPlot::SetNextAxesLimits(
        time_us.front(), time_us.back(),
        y_min, y_max, ImPlotCond_Always);

    if (ImPlot::BeginPlot("##IQ")) {
        ImPlot::SetupAxes("Time (us)", "Amplitude");
        ImPlot::PlotLine("I", time_us.data(), i_vals.data(), static_cast<int>(n));
        ImPlot::PlotLine("Q", time_us.data(), q_vals.data(), static_cast<int>(n));
        ImPlot::EndPlot();
    }

    ImGui::End();
}

void IQPlotWidget::runIDFT() {
    auto& out = m_pfb.node().outputs[0];

    size_t N = out.frequencies.size();
    if (N < 2) return;

    double bin_width = (N > 1)
        ? (out.frequencies.back() - out.frequencies.front()) / static_cast<double>(N - 1)
        : 1.0;

    // Build complex spectrum from weighted noise + tones
    std::vector<std::complex<double>> spectrum(N, {0.0, 0.0});
    for (size_t i = 0; i < N; ++i) {
        double psd = (i < out.noise_total_W.size()) ? out.noise_total_W[i] : 0.0;
        double magnitude = std::sqrt(std::max(0.0, psd * bin_width));
        double phase_rad = (i < out.phase_deg.size()) ? out.phase_deg[i] * M_PI / 180.0 : 0.0;
        spectrum[i] = std::complex<double>(magnitude * std::cos(phase_rad), magnitude * std::sin(phase_rad));
    }

    for (const auto& tone : out.tones) {
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

    // IDFT via kiss_fft
    std::vector<double> td_i(N), td_q(N);
    {
        std::vector<kiss_fft_cpx> fd(N), td(N);
        for (size_t i = 0; i < N; ++i) {
            fd[i].r = static_cast<float>(spectrum[i].real());
            fd[i].i = static_cast<float>(spectrum[i].imag());
        }

        kiss_fft_cfg ifft = kiss_fft_alloc(static_cast<int>(N), 1, nullptr, nullptr);
        kiss_fft(ifft, fd.data(), td.data());
        kiss_fft_free(ifft);

        for (size_t i = 0; i < N; ++i) {
            td_i[i] = static_cast<double>(td[i].r) / static_cast<double>(N);
            td_q[i] = static_cast<double>(td[i].i) / static_cast<double>(N);
        }
    }

    pushSamples(td_i, td_q);
}

void IQPlotWidget::pushSamples(const std::vector<double>& i, const std::vector<double>& q) {
    for (size_t j = 0; j < i.size(); ++j) {
        m_stream_i.push_back(i[j]);
        m_stream_q.push_back(q[j]);
    }
    m_time_cursor_s += static_cast<double>(i.size()) * m_time_step_s;

    // Trim to max buffer size
    while (m_stream_i.size() > kMaxSamples) {
        m_stream_i.pop_front();
        m_stream_q.pop_front();
    }
}
