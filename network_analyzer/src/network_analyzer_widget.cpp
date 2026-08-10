#include "network_analyzer_widget.h"
#include "imgui.h"
#include "implot.h"
#include <cmath>

void NetworkAnalyzerWidget::draw(const char *title, bool *p_open) {
    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    const auto &freqs = m_engine.sweepFrequencies();
    const auto &gain = m_engine.gainDb();
    const auto &nf = m_engine.noiseFigureDb();

    if (ImPlot::BeginPlot("Gain / Noise Figure vs Frequency", ImVec2(-1, -80))) {
        ImPlot::SetupAxes("Frequency (Hz)", "dB");
        if (!freqs.empty() && freqs.size() == gain.size() && freqs.size() == nf.size()) {
            ImPlot::PlotLine("Gain (dB)", freqs.data(), gain.data(),
                             static_cast<int>(freqs.size()));
            ImPlot::PlotLine("Noise Figure (dB)", freqs.data(), nf.data(),
                             static_cast<int>(freqs.size()));
        }
        ImPlot::EndPlot();
    }

    double gain_sum = 0.0, nf_sum = 0.0;
    int gain_n = 0, nf_n = 0;
    for (double g : gain) {
        if (std::isfinite(g)) {
            gain_sum += g;
            ++gain_n;
        }
    }
    for (double n : nf) {
        if (std::isfinite(n)) {
            nf_sum += n;
            ++nf_n;
        }
    }

    if (gain_n > 0)
        ImGui::Text("Avg Gain: %.2f dB", gain_sum / gain_n);
    else
        ImGui::TextDisabled("Avg Gain: no data");

    if (nf_n > 0)
        ImGui::Text("Avg NF: %.2f dB", nf_sum / nf_n);
    else
        ImGui::TextDisabled("Avg NF: no data");

    ImGui::End();
}
