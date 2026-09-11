#include "network_analyzer_widget.h"
#include "imgui.h"
#include "implot.h"
#include "node_graph_engine.h"
#include "utils.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

// Axis limits framed on the plotted sweep and its measured values.
struct PlotLimits {
    bool valid = false;
    double x_min = 0.0;
    double x_max = 1.0;
    double y_min = 0.0;
    double y_max = 1.0;
};

// Minimum dB span so a flat trace (constant gain, or a single-point sweep)
// still gets a readable axis instead of a near-zero window.
constexpr double kMinDbSpan = 10.0;
// Headroom kept above and below the measured values, as a fraction of the span.
constexpr double kDbMargin = 0.1;

// ImPlot auto-fits a plot only on the frame it is first drawn and never
// again, so a panel opened before any probe point is selected -- or a sweep
// changed afterwards -- keeps the range that was current then and squeezes the
// traces into a corner. Frame the axes on the current data every frame
// instead, as SpectrumAnalyzerWidget and IQPlotWidget do.
PlotLimits plotLimitsFor(const std::vector<double> &freqs, const std::vector<double> &gain,
                         const std::vector<double> &nf) {
    PlotLimits limits;
    if (freqs.empty() || freqs.size() != gain.size() || freqs.size() != nf.size())
        return limits; // nothing is plotted: leave ImPlot's own range alone

    const auto freq_ends = std::minmax_element(freqs.begin(), freqs.end());
    limits.x_min = *freq_ends.first;
    limits.x_max = *freq_ends.second;

    double lo = 0.0;
    double hi = 0.0;
    bool have_sample = false;
    for (const auto *series : {&gain, &nf}) {
        for (double value : *series) {
            if (!std::isfinite(value))
                continue; // no path, or no matching tone, at that point
            if (!have_sample) {
                lo = hi = value;
                have_sample = true;
            } else {
                lo = std::min(lo, value);
                hi = std::max(hi, value);
            }
        }
    }
    if (!have_sample)
        return limits; // nothing measured to frame

    const double center = 0.5 * (lo + hi);
    const double half_span = std::max(0.5 * (hi - lo), 0.5 * kMinDbSpan) * (1.0 + kDbMargin);
    limits.y_min = center - half_span;
    limits.y_max = center + half_span;
    limits.valid = true;
    return limits;
}

} // namespace

NetworkAnalyzerWidget::NetworkAnalyzerWidget(NetworkAnalyzerEngine &engine, NodeGraphEngine &graph)
    : m_engine(engine), m_graph(graph) {}

void NetworkAnalyzerWidget::draw(const char *title, bool *p_open) {
    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    m_param_edited = false;

    // Point pickers: every real output pin currently in the graph, rebuilt
    // each frame so add/remove/link edits show up immediately. Index 0 = unset.
    struct PinEntry {
        int pin_id;
        std::string label;
    };
    std::vector<PinEntry> pins;
    for (const auto &node : m_graph.nodes()) {
        for (size_t i = 0; i < node.output_pin_ids.size(); ++i) {
            pins.push_back({node.output_pin_ids[i], node.label + " OUT" + std::to_string(i + 1)});
        }
    }
    std::vector<const char *> items;
    items.reserve(pins.size() + 1);
    items.push_back("(none)");
    for (const auto &pin : pins)
        items.push_back(pin.label.c_str());

    int a_idx = 0;
    for (size_t i = 0; i < pins.size(); ++i) {
        if (pins[i].pin_id == m_engine.pointAPin()) {
            a_idx = static_cast<int>(i) + 1;
            break;
        }
    }
    if (ImGui::Combo("Point A (Reference)", &a_idx, items.data(), static_cast<int>(items.size()))) {
        m_engine.setPointA(a_idx == 0 ? -1 : pins[static_cast<size_t>(a_idx - 1)].pin_id);
        m_param_edited = true;
    }

    int b_idx = 0;
    for (size_t i = 0; i < pins.size(); ++i) {
        if (pins[i].pin_id == m_engine.pointBPin()) {
            b_idx = static_cast<int>(i) + 1;
            break;
        }
    }
    if (ImGui::Combo("Point B (Measured)", &b_idx, items.data(), static_cast<int>(items.size()))) {
        m_engine.setPointB(b_idx == 0 ? -1 : pins[static_cast<size_t>(b_idx - 1)].pin_id);
        m_param_edited = true;
    }

    // Sweep parameters — same widgets/ranges as the v1/v2 Inspector fields.
    double f0 = m_engine.startFrequency();
    if (utils::inputDouble("Start Freq (Hz)", f0, 1e6, 1e7, "%.0f", 0.0, 20e9)) {
        m_engine.setStartFrequency(f0);
        m_param_edited = true;
    }
    double f1 = m_engine.stopFrequency();
    if (utils::inputDouble("Stop Freq (Hz)", f1, 1e6, 1e7, "%.0f", 0.0, 20e9)) {
        m_engine.setStopFrequency(f1);
        m_param_edited = true;
    }
    int pts = m_engine.points();
    if (ImGui::InputInt("Points", &pts)) {
        m_engine.setPoints(pts);
        m_param_edited = true;
    }
    double power = m_engine.stimulusPower();
    if (utils::inputDouble("Stimulus Power (dBm)", power, 1, 10, "%.1f", -60.0, 10.0)) {
        m_engine.setStimulusPower(power);
        m_param_edited = true;
    }

    ImGui::Separator();

    const auto &freqs = m_engine.sweepFrequencies();
    const auto &gain = m_engine.gainDb();
    const auto &nf = m_engine.noiseFigureDb();

    // Frame the axes on the current sweep and measurement (see plotLimitsFor).
    const PlotLimits limits = plotLimitsFor(freqs, gain, nf);
    if (limits.valid)
        ImPlot::SetNextAxesLimits(limits.x_min, limits.x_max, limits.y_min, limits.y_max,
                                  ImPlotCond_Always);

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

    if (m_param_edited && onParamChange)
        onParamChange();

    ImGui::End();
}
