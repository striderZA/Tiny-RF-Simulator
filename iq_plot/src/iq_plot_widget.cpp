#define _USE_MATH_DEFINES
#include "iq_plot_widget.h"
#include "iq_plot_dsp.h"
#include "imgui.h"
#include "implot.h"
#include <kiss_fft.h>
#include <algorithm>
#include <cmath>
#include <complex>

IQPlotWidget::~IQPlotWidget() {
    if (m_ifft) kiss_fft_free(m_ifft);
}

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

    if (Fs <= 0.0) {
        ImGui::Text("Waiting for sample rate...");
        ImGui::End();
        return;
    }

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

    size_t n = m_stream_i.size();
    double window_us = static_cast<double>(n) * m_time_step_s * 1e6;

    std::vector<double> time_us(n);
    std::vector<double> i_vals(m_stream_i.begin(), m_stream_i.end());
    std::vector<double> q_vals(m_stream_q.begin(), m_stream_q.end());

    // Time axis: 0 to window_us, oldest samples trimmed from front as buffer fills
    double total_us = 0.0;
    for (size_t i = 0; i < n; ++i) {
        time_us[i] = total_us;
        total_us += m_time_step_s * 1e6;
    }

    ImGui::Text("Ch %d/%d | dt=%.1f ns | Window: %.1f us (%zu samp)",
        m_pfb.activeChannel(), m_pfb.channelCount(),
        m_time_step_s * 1e9, window_us, n);

    double x_min = m_zoom_locked ? m_zoom_locked_xmin : 0.0;
    double x_max = m_zoom_locked ? m_zoom_locked_xmax : window_us;

    double raw_min = *std::min_element(i_vals.begin(), i_vals.end());
    double raw_max = *std::max_element(i_vals.begin(), i_vals.end());
    for (double v : q_vals) {
        raw_min = std::min(raw_min, v);
        raw_max = std::max(raw_max, v);
    }

    if (!m_y_inited) {
        m_smooth_y_min = raw_min;
        m_smooth_y_max = raw_max;
        m_y_inited = true;
    } else {
        m_smooth_y_min += kYAlpha * (raw_min - m_smooth_y_min);
        m_smooth_y_max += kYAlpha * (raw_max - m_smooth_y_max);
    }
    double y_min = m_smooth_y_min;
    double y_max = m_smooth_y_max;
    double y_margin = (y_max - y_min) * 0.1 + 1e-18;
    y_min -= y_margin;
    y_max += y_margin;

    ImPlot::SetNextAxesLimits(x_min, x_max, y_min, y_max, ImPlotCond_Always);

    if (ImPlot::BeginPlot("##IQ")) {
        ImPlot::SetupAxes("Time (us)", "Amplitude");
        ImPlot::PlotLine("I", time_us.data(), i_vals.data(), static_cast<int>(n));
        ImPlot::PlotLine("Q", time_us.data(), q_vals.data(), static_cast<int>(n));

        // Drag-to-zoom
        if (ImPlot::IsPlotHovered()) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImPlotPoint p = ImPlot::GetPlotMousePos();
                m_zoom.active = true;
                m_zoom.x_min = p.x;
                m_zoom.x_max = p.x;
            }
            if (m_zoom.active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                ImPlotPoint p = ImPlot::GetPlotMousePos();
                m_zoom.x_max = p.x;
            }
        }

        if (m_zoom.active) {
            double x1 = m_zoom.x_min;
            double x2 = m_zoom.x_max;
            if (std::abs(x2 - x1) > 1e-12) {
                ImPlotPoint p1 = ImPlot::PlotToPixels(x1, y_min);
                ImPlotPoint p2 = ImPlot::PlotToPixels(x2, y_max);
                ImPlot::GetPlotDrawList()->AddRectFilled(
                    ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y),
                    IM_COL32(100, 150, 255, 40));
            }
        }

        ImPlot::EndPlot();
    }

    if (m_zoom.active && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_zoom.active = false;
        double lo = std::min(m_zoom.x_min, m_zoom.x_max);
        double hi = std::max(m_zoom.x_min, m_zoom.x_max);
        if (hi - lo > 1e-12) {
            m_zoom_locked = true;
            m_zoom_locked_xmin = lo;
            m_zoom_locked_xmax = hi;
        }
    }

    if (m_zoom_locked && ImGui::Button("Reset Zoom")) {
        m_zoom_locked = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Auto Scale")) {
        m_y_inited = false;
    }

    ImGui::End();
}

void IQPlotWidget::runIDFT() {
    auto& out = m_pfb.node().outputs[0];

    size_t N = out.frequencies.size();
    if (N < 2) return;

    // Build frequency-domain spectrum via extracted DSP helper
    std::vector<std::complex<double>> spectrum;
    if (!build_iq_spectrum(out.frequencies, out.noise_total_W, out.phase_deg, out.tones, spectrum)) {
        return;
    }


    std::vector<double> td_i(N), td_q(N);
    {
        std::vector<kiss_fft_cpx> fd(N), td(N);
        for (size_t i = 0; i < N; ++i) {
            fd[i].r = static_cast<float>(spectrum[i].real());
            fd[i].i = static_cast<float>(spectrum[i].imag());
        }

        if (N != m_ifft_N) {
            if (m_ifft) kiss_fft_free(m_ifft);
            m_ifft = kiss_fft_alloc(static_cast<int>(N), 1, nullptr, nullptr);
            m_ifft_N = N;
        }
        kiss_fft(m_ifft, fd.data(), td.data());

        for (size_t i = 0; i < N; ++i) {
            td_i[i] = static_cast<double>(td[i].r) / static_cast<double>(N);
            td_q[i] = static_cast<double>(td[i].i) / static_cast<double>(N);
        }
    }

    // Append to buffer, trim front
    for (size_t j = 0; j < N; ++j) {
        m_stream_i.push_back(td_i[j]);
        m_stream_q.push_back(td_q[j]);
    }
    while (m_stream_i.size() > kMaxSamples) {
        m_stream_i.pop_front();
        m_stream_q.pop_front();
    }
}
