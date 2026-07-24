#include "pfb_channelizer_widget.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

PFBChannelizerWidget::PFBChannelizerWidget(PFBChannelizerEngine &engine) : m_engine(engine) {}

void PFBChannelizerWidget::rebuildCache() {
    const Spectrum *in = m_engine.node().inputs[0];
    if (!in || in->frequencies.size() < 2) {
        m_cells.clear();
        return;
    }

    double bin_width = in->frequencies[1] - in->frequencies[0];
    const auto &channels = m_engine.channels();
    int M = static_cast<int>(channels.size());
    int start = std::min(m_grid_offset, std::max(0, M - 1));
    int count = std::min(16, M - start);
    m_cells.resize(count);

    for (int ci = 0; ci < count; ++ci) {
        auto &ch = channels[start + ci];
        auto &cell = m_cells[ci];
        size_t n = ch.bin_indices.size();
        cell.freqs.resize(n);
        cell.power_dBm.resize(n);
        cell.tones = ch.tones;

        for (size_t i = 0; i < n; ++i) {
            int bi = ch.bin_indices[i];
            double w = ch.bin_weights[i];
            if (bi < 0 || bi >= static_cast<int>(in->frequencies.size())) {
                cell.freqs[i] = 0.0;
                cell.power_dBm[i] = -200.0;
                continue;
            }
            cell.freqs[i] = in->frequencies[bi];
            double psd =
                (bi < static_cast<int>(in->noise_total_W.size())) ? in->noise_total_W[bi] : 0.0;
            double power_W = psd * w * w * bin_width;
            cell.power_dBm[i] = 10.0 * std::log10(power_W / 0.001 + 1e-100);
        }
    }
    m_y_min = 1e30;
    m_y_max = -1e30;
    for (auto &c : m_cells) {
        for (auto v : c.power_dBm) {
            if (v < m_y_min)
                m_y_min = v;
            if (v > m_y_max)
                m_y_max = v;
        }
    }
    double y_range = m_y_max - m_y_min;
    if (y_range < 10.0) {
        m_y_min -= 5.0;
        m_y_max += 5.0;
        y_range = m_y_max - m_y_min;
    }
    m_y_min -= 3.0;
    m_y_max += 3.0;

    m_cached_gen = in->generation;
}

void PFBChannelizerWidget::draw(const char *title, bool *p_open) {
    ImGui::SetNextWindowSize(ImVec2(800, 700), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    const Spectrum *in = m_engine.node().inputs[0];
    int M = static_cast<int>(m_engine.channels().size());

    if (!in || M == 0) {
        ImGui::TextDisabled("No PFB data available");
        ImGui::End();
        return;
    }

    if (M > 16) {
        ImGui::SetNextItemWidth(300.0f);
        int max_offset = M - 16;
        ImGui::SliderInt("Channel Offset", &m_grid_offset, 0, max_offset);
    } else {
        m_grid_offset = 0;
    }

    if (in->generation != m_cached_gen)
        rebuildCache();

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetCursorScreenPos();
    ImVec2 win_size = ImGui::GetContentRegionAvail();

    const float spacing = 4.0f;
    float cell_w = (win_size.x - spacing * 5) / 4.0f;
    float cell_h = (win_size.y - spacing * 5) / 4.0f;

    if (cell_w < 30 || cell_h < 30) {
        ImGui::TextDisabled("Resize window larger to see channel grid");
        ImGui::End();
        return;
    }

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            int idx = row * 4 + col;
            if (idx >= static_cast<int>(m_cells.size()))
                break;

            ImVec2 c0(win_pos.x + spacing + col * (cell_w + spacing),
                      win_pos.y + spacing + row * (cell_h + spacing));
            ImVec2 c1(c0.x + cell_w, c0.y + cell_h);
            ImRect cell_rect(c0, c1);
            drawCell(dl, cell_rect, m_grid_offset + idx);
        }
    }

    ImGui::Dummy(win_size);
    ImGui::End();
}

void PFBChannelizerWidget::drawCell(ImDrawList *dl, const ImRect &rect, int ch_idx) {
    if (ch_idx < 0 || ch_idx >= static_cast<int>(m_engine.channels().size()))
        return;

    bool active = (ch_idx == m_engine.activeChannel());

    dl->AddRectFilled(rect.Min, rect.Max, IM_COL32(12, 12, 12, 255));

    ImU32 border_col = active ? IM_COL32(255, 165, 0, 255) : IM_COL32(50, 50, 50, 255);
    dl->AddRect(rect.Min, rect.Max, border_col, 0.0f, 0, active ? 2.5f : 1.0f);

    const auto &ch = m_engine.channels()[ch_idx];
    char title[64];
    snprintf(title, sizeof(title), "Ch %d  %.1f MHz", ch_idx, ch.center_freq_Hz / 1e6);
    dl->AddText(ImVec2(rect.Min.x + 4, rect.Min.y + 2), IM_COL32(180, 180, 180, 255), title);

    const float header_h = 18.0f;
    const float margin = 3.0f;
    ImRect plot(rect.Min.x + margin, rect.Min.y + header_h, rect.Max.x - margin,
                rect.Max.y - margin);
    if (plot.GetWidth() < 10 || plot.GetHeight() < 10)
        return;

    int cache_idx = ch_idx - m_grid_offset;
    if (cache_idx < 0 || cache_idx >= static_cast<int>(m_cells.size()))
        return;
    auto &cell = m_cells[cache_idx];
    if (cell.freqs.empty() || cell.power_dBm.empty())
        return;

    double y_range = m_y_max - m_y_min;

    double x_min = cell.freqs.front();
    double x_max = cell.freqs.back();
    double x_range = x_max - x_min;
    if (x_range <= 0.0 || y_range <= 0.0)
        return;

    auto toScreen = [&](double fx, double fy) -> ImVec2 {
        float px = plot.Min.x + static_cast<float>((fx - x_min) / x_range) * plot.GetWidth();
        float py = plot.Max.y - static_cast<float>((fy - m_y_min) / y_range) * plot.GetHeight();
        return ImVec2(px, py);
    };

    if (cell.power_dBm.size() >= 2) {
        dl->PathClear();
        dl->PathLineTo(toScreen(cell.freqs[0], cell.power_dBm[0]));
        for (size_t i = 1; i < cell.power_dBm.size(); ++i) {
            if (i >= cell.freqs.size())
                break;
            dl->PathLineTo(toScreen(cell.freqs[i], cell.power_dBm[i]));
        }
        dl->PathStroke(IM_COL32(23, 200, 153, 255), false, 1.5f);
    }

    for (auto &t : cell.tones) {
        double tone_power_W = std::pow(10.0, t.power_dBm / 10.0) * 0.001;
        for (size_t i = 0; i + 1 < cell.freqs.size(); ++i) {
            if (cell.freqs[i] <= t.freq_Hz && t.freq_Hz <= cell.freqs[i + 1]) {
                double noise_W = std::pow(10.0, cell.power_dBm[i] / 10.0) * 0.001;
                double total_dBm = 10.0 * std::log10((tone_power_W + noise_W) / 0.001 + 1e-100);
                ImVec2 pos = toScreen(t.freq_Hz, total_dBm);
                if (plot.Contains(pos))
                    dl->AddCircleFilled(pos, 3.0f, IM_COL32(255, 128, 0, 255));
                break;
            }
        }
    }

    if (ImGui::IsMouseHoveringRect(rect.Min, rect.Max, false) &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_engine.setActiveChannel(ch_idx);
    }
}
