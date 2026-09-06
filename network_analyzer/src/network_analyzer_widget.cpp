#include "network_analyzer_widget.h"
#include "imgui.h"
#include "implot.h"
#include "node_graph_engine.h"
#include "utils.h"
#include <cmath>
#include <string>
#include <vector>

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
