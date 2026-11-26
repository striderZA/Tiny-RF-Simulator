#include "spectrum_analyzer_widget.h"
#include "common.h"
#include "imgui.h"
#include "implot.h"
#include "logging_core.h"
#include "pfb_channelizer_engine.h"
#include "utils.h"
#include "view_manager.h"
#include <algorithm>
#include <cstdio>
#include <limits>

namespace {
// Vertical space (px) the rows below the plot need: the average-noise text,
// the trace/peak text, Reset Zoom, and the always-visible marker row (M1..M4
// and the Marker Table toggle). Reserving it lets the plot grow into whatever
// is left instead of sitting at ImPlot's fixed 300 px default (issue #88).
constexpr float kPlotBottomReserve = 112.0f;

// Marker colors stay distinct from the four probe trace colors (teal, orange,
// purple, blue) so a marker is never mistaken for a trace.
const ImVec4 kMarkerColors[MarkerModel::kMaxMarkers] = {
    ImVec4(1.00f, 0.85f, 0.20f, 1.0f), // M1 yellow (reference)
    ImVec4(0.95f, 0.35f, 0.85f, 1.0f), // M2 magenta
    ImVec4(0.30f, 0.85f, 1.00f, 1.0f), // M3 cyan
    ImVec4(1.00f, 0.35f, 0.35f, 1.0f), // M4 red
};
const char *const kMarkerNames[MarkerModel::kMaxMarkers] = {"M1", "M2", "M3", "M4"};
} // namespace

SpectrumAnalyzerWidget::SpectrumAnalyzerWidget(SpectrumAnalyzerEngine &engine,
                                               const ViewManager &vm)
    : m_engine(engine), m_view_manager(vm) {}

void SpectrumAnalyzerWidget::setPFBs(const std::vector<PFBChannelizerEngine *> &pfbs) {
    m_pfb_ptrs = pfbs;
    m_pfb_map.clear();
    for (auto *pfb : m_pfb_ptrs)
        if (pfb)
            m_pfb_map[&pfb->node()] = pfb;
}

void SpectrumAnalyzerWidget::setProbeTargets(
    const std::vector<std::pair<SignalNode *, int>> &targets) {
    m_probe_targets = targets;
    m_probe_output_index.clear();
    for (const auto &[node, output_index] : targets)
        if (node)
            m_probe_output_index[node] = output_index;
}

const Spectrum &SpectrumAnalyzerWidget::outputSpectrumFor(SignalNode *node) const {
    auto it = m_probe_output_index.find(node);
    if (it != m_probe_output_index.end() && it->second >= 0 &&
        static_cast<size_t>(it->second) < node->outputs.size()) {
        return node->outputs[static_cast<size_t>(it->second)];
    }
    static const Spectrum kEmpty;
    return node->outputs.empty() ? kEmpty : node->outputs[0];
}

std::vector<MeasurementProbeId> SpectrumAnalyzerWidget::measurementProbeIds() const {
    std::vector<MeasurementProbeId> ids;
    ids.reserve(m_probe_targets.size());
    for (const auto &[node, output_index] : m_probe_targets) {
        ids.push_back(MeasurementProbeId{node, output_index});
    }
    return ids;
}

const Spectrum *SpectrumAnalyzerWidget::spectrumForProbeSlot(size_t slot) const {
    if (slot >= m_probe_targets.size())
        return nullptr;
    SignalNode *node = m_probe_targets[slot].first;
    if (!node || node->outputs.empty())
        return nullptr;
    const int output_index = m_probe_targets[slot].second;
    if (output_index >= 0 && static_cast<size_t>(output_index) < node->outputs.size())
        return &node->outputs[static_cast<size_t>(output_index)];
    return &node->outputs[0];
}

float SpectrumAnalyzerWidget::markerAreaReserve() const {
    // The Peak Table toggle always has its own row under the marker row.
    float reserve = ImGui::GetFrameHeightWithSpacing();
    if (m_markers.tableVisible()) {
        // A table row is taller than a text line (cell padding plus the row
        // background), so reserve header and rows at frame height.
        const int rows = std::max(1, m_markers.enabledCount());
        reserve += ImGui::GetFrameHeightWithSpacing() * static_cast<float>(rows + 1) + 8.0f;
    }
    if (m_peaks.visible()) {
        // Header row, the step buttons, and one row per peak (the search may find none).
        const int rows = std::max(1, m_peaks.count());
        reserve += ImGui::GetFrameHeightWithSpacing() * static_cast<float>(rows + 2) + 8.0f;
    }
    if (m_harmonics.visible()) {
        // The use-marker row, the fundamental line, and the header plus two harmonic rows.
        reserve += ImGui::GetFrameHeightWithSpacing() * 4.0f +
                   ImGui::GetTextLineHeightWithSpacing() + 8.0f;
    }
    if (!m_markers.anyEnabled())
        return reserve;

    // Selected-marker combo/delta row and the peak-search button row.
    reserve += 2.0f * ImGui::GetFrameHeightWithSpacing();
    // One compact readout line per enabled marker.
    reserve += ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(m_markers.enabledCount());
    return reserve;
}

bool SpectrumAnalyzerWidget::drawMarkersOnPlot(const std::vector<double> &freq_axis,
                                               const std::vector<double> &display_dBm) {
    bool marker_tool_active = false;
    for (int i = 0; i < MarkerModel::kMaxMarkers; ++i) {
        if (!m_markers.enabled(i))
            continue;

        // Instrument-style vertical line: grabbing it anywhere moves the marker
        // along the frequency axis, not just grabbing the point symbol.
        double x = m_markers.frequency(i);
        bool clicked = false;
        bool hovered = false;
        bool held = false;
        ImPlot::DragLineX(i + 1, &x, kMarkerColors[i], i == m_markers.selected() ? 2.0f : 1.0f,
                          ImPlotDragToolFlags_NoFit, &clicked, &hovered, &held);
        if (clicked || held)
            m_markers.select(i);
        if (held)
            m_drag_marker = i;
        marker_tool_active = marker_tool_active || hovered || held;
        m_markers.place(i, x);

        const MarkerModel::Readout reading = m_markers.readout(i, freq_axis, display_dBm);
        if (!reading.valid)
            continue;

        char point_id[24];
        char name[8];
        std::snprintf(point_id, sizeof point_id, "##markerpoint%d", i + 1);
        std::snprintf(name, sizeof name, "M%d", i + 1);

        ImPlotSpec point_spec;
        point_spec.Marker = ImPlotMarker_Circle;
        point_spec.MarkerSize = 6.0f;
        point_spec.MarkerFillColor = kMarkerColors[i];
        point_spec.MarkerLineColor = kMarkerColors[i];
        point_spec.LineWeight = 1.0f;
        point_spec.Flags = ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
        ImPlot::PlotScatter(point_id, &reading.frequency_Hz, &reading.amplitude_dBm, 1, point_spec);

        ImPlotSpec text_spec;
        text_spec.Flags = ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
        ImPlot::PushStyleColor(ImPlotCol_InlayText, kMarkerColors[i]);
        ImPlot::PlotText(name, reading.frequency_Hz, reading.amplitude_dBm, ImVec2(5.0f, -18.0f),
                         text_spec);
        ImPlot::PopStyleColor();
    }

    // A released drag settles on the nearest displayed bin, so the readout and
    // the line always name the same trace sample.
    if (m_drag_marker >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const int idx = m_markers.resolveIndex(m_drag_marker, freq_axis, display_dBm);
        if (idx >= 0)
            m_markers.place(m_drag_marker, freq_axis[static_cast<size_t>(idx)]);
        m_drag_marker = -1;
    }
    return marker_tool_active;
}

void SpectrumAnalyzerWidget::drawMarkerControls(const std::vector<double> &freq_axis,
                                                const std::vector<double> &display_dBm) {
    for (int i = 0; i < MarkerModel::kMaxMarkers; ++i) {
        bool on = m_markers.enabled(i);
        ImGui::PushStyleColor(ImGuiCol_Text, kMarkerColors[i]);
        if (ImGui::Checkbox(kMarkerNames[i], &on)) {
            m_markers.setEnabled(i, on);
            m_markers.select(i);
            if (on) {
                m_markers.place(i, freq_axis.empty() ? 0.0 : freq_axis[freq_axis.size() / 2]);
            }
        }
        ImGui::PopStyleColor();
        if (i + 1 < MarkerModel::kMaxMarkers)
            ImGui::SameLine();
    }
    ImGui::SameLine();
    bool table_visible = m_markers.tableVisible();
    if (ImGui::Checkbox("Marker Table", &table_visible))
        m_markers.setTableVisible(table_visible);

    // On its own row: a narrow dock cannot fit a fifth control beside the marker row.
    bool peaks_visible = m_peaks.visible();
    if (ImGui::Checkbox("Peak Table", &peaks_visible))
        m_peaks.setVisible(peaks_visible);
    ImGui::SameLine();
    bool harmonics_visible = m_harmonics.visible();
    if (ImGui::Checkbox("Harmonics", &harmonics_visible))
        m_harmonics.setVisible(harmonics_visible);

    if (!m_markers.anyEnabled())
        return;

    int selected = m_markers.selected();
    ImGui::SetNextItemWidth(70.0f);
    if (ImGui::Combo("Marker", &selected, kMarkerNames, MarkerModel::kMaxMarkers))
        m_markers.select(selected);

    ImGui::SameLine();
    const bool is_reference = m_markers.selected() == MarkerModel::kReferenceIndex;
    bool delta = m_markers.delta(m_markers.selected());
    if (is_reference)
        ImGui::BeginDisabled();
    if (ImGui::Checkbox("Delta (vs M1)", &delta))
        m_markers.setDelta(m_markers.selected(), delta);
    if (is_reference) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui::BeginTooltip()) {
            ImGui::TextUnformatted("M1 is the reference marker; it always reads absolute.");
            ImGui::EndTooltip();
        }
    }

    if (ImGui::Button("Snap to Peak"))
        m_markers.snapToPeak(m_markers.selected(), m_engine.findPeaks(display_dBm, freq_axis, 8));
    ImGui::SameLine();
    if (ImGui::Button("Next Peak")) {
        std::vector<Peak> peaks;
        if (m_markers.peaks(m_markers.selected()).empty())
            peaks = m_engine.findPeaks(display_dBm, freq_axis, 8);
        m_markers.nextPeak(m_markers.selected(), peaks);
    }

    // Compact readout strip: one line per enabled marker, in the marker color.
    for (int i = 0; i < MarkerModel::kMaxMarkers; ++i) {
        if (!m_markers.enabled(i))
            continue;
        const MarkerModel::Readout reading = m_markers.readout(i, freq_axis, display_dBm);
        char line[160];
        if (!reading.valid) {
            std::snprintf(line, sizeof line, "M%d  --", i + 1);
        } else if (reading.delta) {
            if (reading.reference_valid) {
                std::snprintf(line, sizeof line, "M%d delta  %+.3f MHz  %+.2f dB", i + 1,
                              reading.delta_frequency_Hz / 1e6, reading.delta_amplitude_dB);
            } else {
                std::snprintf(line, sizeof line, "M%d delta  (enable M1 reference)", i + 1);
            }
        } else {
            std::snprintf(line, sizeof line, "M%d  %.3f MHz  %.2f dBm", i + 1,
                          reading.frequency_Hz / 1e6, reading.amplitude_dBm);
        }

        char id[24];
        std::snprintf(id, sizeof id, "###marker%d", i + 1);
        const std::string label = std::string(line) + id;
        ImGui::PushStyleColor(ImGuiCol_Text, kMarkerColors[i]);
        if (ImGui::Selectable(label.c_str(), i == m_markers.selected(), ImGuiSelectableFlags_None,
                              ImVec2(0.0f, 0.0f))) {
            m_markers.select(i);
        }
        ImGui::PopStyleColor();
    }
}

void SpectrumAnalyzerWidget::drawMarkerTable(const std::vector<double> &freq_axis,
                                             const std::vector<double> &display_dBm) {
    if (!m_markers.tableVisible())
        return;
    if (!m_markers.anyEnabled()) {
        ImGui::TextUnformatted("Marker table: no markers enabled.");
        return;
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter;
    if (!ImGui::BeginTable("Marker Table", 5, flags))
        return;
    // Short unit headers keep the five columns inside a 400 px dock.
    ImGui::TableSetupColumn("Marker");
    ImGui::TableSetupColumn("MHz");
    ImGui::TableSetupColumn("dBm");
    ImGui::TableSetupColumn("df MHz");
    ImGui::TableSetupColumn("dAmp dB");
    ImGui::TableHeadersRow();

    for (int i = 0; i < MarkerModel::kMaxMarkers; ++i) {
        if (!m_markers.enabled(i))
            continue;
        const MarkerModel::Readout reading = m_markers.readout(i, freq_axis, display_dBm);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        char selectable[24];
        std::snprintf(selectable, sizeof selectable, "M%d###markertable%d", i + 1, i + 1);
        ImGui::PushStyleColor(ImGuiCol_Text, kMarkerColors[i]);
        if (ImGui::Selectable(selectable, i == m_markers.selected()))
            m_markers.select(i);
        ImGui::PopStyleColor();

        ImGui::TableSetColumnIndex(1);
        if (reading.valid)
            ImGui::Text("%.3f", reading.frequency_Hz / 1e6);
        else
            ImGui::TextUnformatted("-");
        ImGui::TableSetColumnIndex(2);
        if (reading.valid)
            ImGui::Text("%.2f", reading.amplitude_dBm);
        else
            ImGui::TextUnformatted("-");
        ImGui::TableSetColumnIndex(3);
        if (reading.delta && reading.reference_valid)
            ImGui::Text("%+.3f", reading.delta_frequency_Hz / 1e6);
        else
            ImGui::TextUnformatted("-");
        ImGui::TableSetColumnIndex(4);
        if (reading.delta && reading.reference_valid)
            ImGui::Text("%+.2f", reading.delta_amplitude_dB);
        else
            ImGui::TextUnformatted("-");
    }
    ImGui::EndTable();
}

void SpectrumAnalyzerWidget::applySelectedPeak() {
    const int row = m_peaks.selected();
    if (row < 0 || row >= m_peaks.count())
        return;
    const int marker = m_markers.selected();
    m_markers.setEnabled(marker, true);
    m_markers.place(marker, m_peaks.rows()[static_cast<size_t>(row)].frequency_Hz);
}

void SpectrumAnalyzerWidget::drawPeakTable() {
    if (!m_peaks.visible())
        return;

    ImGui::Separator();
    ImGui::Text("Peak table (strongest first, marker %s)", kMarkerNames[m_markers.selected()]);
    if (ImGui::Button("Prev Row###peakprev")) {
        m_peaks.step(-1);
        applySelectedPeak();
    }
    ImGui::SameLine();
    if (ImGui::Button("Next Row###peaknext")) {
        m_peaks.step(1);
        applySelectedPeak();
    }

    if (m_peaks.count() == 0) {
        ImGui::TextUnformatted("Peak table: no peaks found.");
        return;
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter;
    if (!ImGui::BeginTable("Peak Table", 5, flags))
        return;
    ImGui::TableSetupColumn("#");
    ImGui::TableSetupColumn("MHz");
    ImGui::TableSetupColumn("dBm");
    ImGui::TableSetupColumn("df MHz");
    ImGui::TableSetupColumn("dAmp dB");
    ImGui::TableHeadersRow();

    for (int i = 0; i < m_peaks.count(); ++i) {
        const PeakTableModel::Row &row = m_peaks.rows()[static_cast<size_t>(i)];
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        char selectable[32];
        std::snprintf(selectable, sizeof selectable, "#%d###peaktablerow%d", i + 1, i + 1);
        if (ImGui::Selectable(selectable, i == m_peaks.selected(),
                              ImGuiSelectableFlags_SpanAllColumns)) {
            m_peaks.select(i);
            applySelectedPeak();
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.3f", row.frequency_Hz / 1e6);
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%.2f", row.amplitude_dBm);
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%+.3f", row.delta_frequency_Hz / 1e6);
        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%+.2f", row.delta_amplitude_dB);
    }
    ImGui::EndTable();
}

void SpectrumAnalyzerWidget::drawHarmonicTable() {
    if (!m_harmonics.visible())
        return;

    ImGui::Separator();
    ImGui::TextUnformatted("Harmonic measurement");

    // The marker source follows the selected marker, like Snap to Peak and Next Peak. A
    // marker that is off has no meaningful frequency, so the box is disabled and the
    // measurement falls back to the strongest tone (the line below says which).
    const bool marker_on = m_markers.enabled(m_markers.selected());
    bool use_marker = m_harmonics.useMarker();
    if (!marker_on)
        ImGui::BeginDisabled();
    if (ImGui::Checkbox("Use marker as fundamental", &use_marker))
        m_harmonics.setUseMarker(use_marker);
    if (!marker_on) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui::BeginTooltip()) {
            ImGui::TextUnformatted("Enable a marker to use its frequency as the fundamental.");
            ImGui::EndTooltip();
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", kMarkerNames[m_markers.selected()]);

    const HarmonicModel::Result &result = m_harmonics.result();
    if (result.fundamental_status == HarmonicModel::Status::NoFundamental) {
        if (m_harmonics.useMarker() && marker_on) {
            ImGui::TextUnformatted(
                "No clear fundamental: the marker is at 0 Hz. Place it on a tone.");
        } else {
            ImGui::TextUnformatted(
                "No clear fundamental: no tone stands clear of the noise floor.");
        }
        return;
    }
    if (result.fundamental_status == HarmonicModel::Status::FundamentalOutOfSpan) {
        ImGui::TextUnformatted("No clear fundamental: the marker is outside the current span.");
        return;
    }

    ImGui::Text("Fundamental: %.3f MHz  %.2f dBm  (%s)", result.fundamental_Hz / 1e6,
                result.fundamental_dBm,
                (m_harmonics.useMarker() && marker_on) ? "marker" : "strongest tone");

    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter;
    if (!ImGui::BeginTable("Harmonic Table", 4, flags))
        return;
    // Short unit headers keep the four columns inside a 400 px dock.
    ImGui::TableSetupColumn("Harmonic");
    ImGui::TableSetupColumn("MHz");
    ImGui::TableSetupColumn("dBm");
    ImGui::TableSetupColumn("dBc");
    ImGui::TableHeadersRow();

    for (const HarmonicModel::Entry &entry : result.harmonics) {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (entry.order == 2)
            ImGui::TextUnformatted("2nd");
        else
            ImGui::TextUnformatted("3rd");

        ImGui::TableSetColumnIndex(1);
        switch (entry.status) {
        case HarmonicModel::Status::Measured:
            ImGui::Text("%.3f", entry.frequency_Hz / 1e6);
            break;
        case HarmonicModel::Status::OutOfSpan:
            ImGui::TextUnformatted("outside span");
            break;
        case HarmonicModel::Status::Unresolved:
            ImGui::TextUnformatted(entry.rbw_limited ? "RBW too wide" : "grid too coarse");
            break;
        default:
            ImGui::TextUnformatted("not found");
            break;
        }

        ImGui::TableSetColumnIndex(2);
        if (entry.status == HarmonicModel::Status::Measured)
            ImGui::Text("%.2f", entry.amplitude_dBm);
        else
            ImGui::TextUnformatted("-");

        ImGui::TableSetColumnIndex(3);
        if (entry.status == HarmonicModel::Status::Measured)
            ImGui::Text("%+.2f", entry.relative_dBc);
        else
            ImGui::TextUnformatted("-");
    }
    ImGui::EndTable();
}

void SpectrumAnalyzerWidget::draw(const char *title, bool *p_open) {
    m_readout_dBm.clear();
    m_last_plot_area = {};
    // First-use size gives the plot a useful working area; the minimum stops at
    // the width the labelled inputs need and a modest height so a docked panel
    // never forces its dock node to grow and squeeze neighbouring panels
    // (issue #88).
    ImGui::SetNextWindowSize(ImVec2(640, 620), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(400, 240), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    double s = m_engine.startFrequency();
    double e = m_engine.stopFrequency();
    double vbw = m_engine.vbw();
    double rbw = m_engine.rbw();
    double minp = m_engine.minPower();
    double maxp = m_engine.maxPower();

    if (utils::inputFrequency("Start Frequency (MHz)", s, 1.0, 100.0, "%.0f", MIN_FREQ, MAX_FREQ)) {
        m_engine.setStartFrequency(s);
        LOG_INFO("Update start frequency: %.0f MHz", m_engine.startFrequency() / 1e6);
    }

    if (utils::inputFrequency("Stop Frequency (MHz)", e, 1.0, 100.0, "%.0f", MIN_FREQ, MAX_FREQ)) {
        m_engine.setStopFrequency(e);
        LOG_INFO("Update stop frequency: %.0f MHz", m_engine.stopFrequency() / 1e6);
    }

    // RBW/VBW are displayed in kHz so the range can reach down to 1 kHz: the
    // full sweep is 1 kHz .. 100 MHz and every value in it is typeable without
    // fractional MHz (a 1 kHz setting reads "1", not "0.001").
    if (utils::inputFrequency("VBW (kHz)", vbw, 1.0, 10.0, "%.0f", 1e3, 100e6, 1e3)) {
        m_engine.setVideoBw(vbw);
        LOG_INFO("Update VBW: %.0f kHz", m_engine.vbw() / 1e3);
    }
    if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
        ImGui::TextUnformatted("Video bandwidth: moving-average window applied across\n"
                               "the displayed trace. Wider = smoother noise floor.");
        ImGui::EndTooltip();
    }

    if (utils::inputFrequency("RBW (kHz)", rbw, 1.0, 10.0, "%.0f", 1e3, 100e6, 1e3)) {
        m_engine.setResBw(rbw);
        LOG_INFO("Update RBW: %.0f kHz", m_engine.rbw() / 1e3);
    }
    if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
        ImGui::TextUnformatted("Resolution bandwidth: Gaussian filter width applied to\n"
                               "the spectrum. A narrower RBW lowers the displayed noise\n"
                               "floor even below the bin spacing, but frequency detail\n"
                               "below that spacing cannot be resolved.");
        ImGui::EndTooltip();
    }

    // Trace mode controls
    static const char *trace_mode_labels[] = {"Clear/Write", "Max Hold", "Min Hold", "Video Avg"};
    static const TraceMode trace_mode_values[] = {TraceMode::ClearWrite, TraceMode::MaxHold,
                                                  TraceMode::MinHold, TraceMode::VideoAverage};
    int current_mode_idx = 0;
    for (int i = 0; i < 4; ++i) {
        if (m_engine.traceMode() == trace_mode_values[i]) {
            current_mode_idx = i;
            break;
        }
    }
    if (ImGui::Combo("Trace Mode", &current_mode_idx, trace_mode_labels, 4)) {
        m_engine.setTraceMode(trace_mode_values[current_mode_idx]);
    }

    int avg_count = m_engine.videoAvgCount();
    bool avg_enabled = (m_engine.traceMode() == TraceMode::VideoAverage);
    if (!avg_enabled)
        ImGui::BeginDisabled();
    if (ImGui::SliderInt("Avg Count", &avg_count, 2, 100)) {
        m_engine.setVideoAvgCount(avg_count);
    }
    if (!avg_enabled)
        ImGui::EndDisabled();

    bool show_reset =
        (m_engine.traceMode() == TraceMode::MaxHold || m_engine.traceMode() == TraceMode::MinHold);
    if (show_reset) {
        if (ImGui::Button("Reset Hold")) {
            m_engine.resetTraceHistory();
        }
    }

    if (utils::inputDouble("Ref (dBm)", maxp, 5, 10, "%.0f", MIN_POWER, MAX_POWER)) {
        m_engine.setMaxPower(maxp);
        LOG_INFO("Update ref power: %.0f dBm", m_engine.maxPower());
    }

    if (utils::inputDouble("Min level (dBm)", minp, 5, 10, "%.0f", MIN_POWER, MAX_POWER)) {
        m_engine.setMinPower(minp);
        LOG_INFO("Update min power: %.0f dBm", m_engine.minPower());
    }

    auto active_nodes = m_view_manager.getActiveNodes();

    // Sync visibility tracking vector with current probe count
    if (m_trace_visible.size() != active_nodes.size()) {
        m_trace_visible.assign(active_nodes.size(), true);
    }

    const std::vector<MeasurementProbeId> probe_ids = measurementProbeIds();
    // Visibility is tracked per active node, while probes are listed in probe-pin order.
    // Map each probe identity to its active node's visibility so the two orders may differ.
    std::vector<bool> probe_visible(probe_ids.size(), false);
    for (size_t i = 0; i < probe_ids.size(); ++i) {
        const auto it = std::find(active_nodes.begin(), active_nodes.end(), probe_ids[i].node);
        probe_visible[i] = it != active_nodes.end() &&
                           m_trace_visible[static_cast<size_t>(it - active_nodes.begin())];
    }
    m_measurement_resolution = m_measurement.resolve(probe_ids, probe_visible);

    if (m_probe_labels.empty()) {
        ImGui::Text("No node probed");
    } else {
        std::string probes = "Probes:";
        for (size_t i = 0; i < m_probe_labels.size(); ++i)
            probes += "  [" + std::to_string(i) + "] " + m_probe_labels[i];
        ImGui::TextUnformatted(probes.c_str());

        // Measurement source: every visible trace (the historic combined readout) or one
        // probe's own trace. The chosen probe stays selected while hidden/absent so the
        // fallback below can explain why the readout is back on the visible traces.
        auto probe_label = [&](int index) -> std::string {
            if (index >= 0 && static_cast<size_t>(index) < m_probe_labels.size())
                return m_probe_labels[static_cast<size_t>(index)];
            return "probe";
        };

        std::vector<std::string> measure_items;
        measure_items.reserve(m_probe_labels.size() + 1);
        measure_items.emplace_back("All visible");
        for (size_t i = 0; i < m_probe_labels.size(); ++i)
            measure_items.push_back("[" + std::to_string(i) + "] " + m_probe_labels[i]);

        int measure_index = 0;
        if (!m_measurement.allVisible() && m_measurement_resolution.requested_index >= 0) {
            measure_index = std::min(m_measurement_resolution.requested_index + 1,
                                     static_cast<int>(measure_items.size()) - 1);
        }
        ImGui::SetNextItemWidth(240.0f);
        // BeginCombo rather than Combo: a removed or hidden probe can leave the preview
        // already showing "All visible", and the user must still be able to apply that
        // choice to clear the stored selection.
        if (ImGui::BeginCombo("Measure on", measure_items[measure_index].c_str())) {
            for (int i = 0; i < static_cast<int>(measure_items.size()); ++i) {
                const bool selected = i == measure_index;
                if (ImGui::Selectable(measure_items[i].c_str(), selected)) {
                    measure_index = i;
                    if (i <= 0) {
                        m_measurement.selectAllVisible();
                        m_measure_label.clear();
                    } else {
                        const size_t slot = static_cast<size_t>(i - 1);
                        if (slot < probe_ids.size()) {
                            m_measurement.selectProbe(probe_ids[slot]);
                            m_measure_label = probe_label(static_cast<int>(slot));
                        }
                    }
                    m_measurement_resolution = m_measurement.resolve(probe_ids, probe_visible);
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        std::string source = "Measure on: ";
        if (m_measurement_resolution.allVisible())
            source += "All visible traces";
        else
            source += "[" + std::to_string(m_measurement_resolution.probe_index) + "] " +
                      probe_label(m_measurement_resolution.probe_index);
        if (m_measurement_resolution.fallback == MeasurementSelection::Fallback::Hidden) {
            source += " (probe [" + std::to_string(m_measurement_resolution.requested_index) +
                      "] " + probe_label(m_measurement_resolution.requested_index) + " hidden)";
        } else if (m_measurement_resolution.fallback == MeasurementSelection::Fallback::Removed) {
            source += " (" +
                      (m_measure_label.empty() ? std::string("selected probe")
                                               : "probe " + m_measure_label) +
                      " removed)";
        }
        ImGui::TextUnformatted(source.c_str());
    }
    ImGui::Separator();

    // A new measurement source invalidates cached peak lists: Next Peak must cycle the
    // selected trace's peaks, not the previous source's.
    const bool all_visible_source = m_measurement_resolution.allVisible();
    MeasurementProbeId probe_source{};
    bool have_probe_source = false;
    if (!all_visible_source) {
        const size_t slot = static_cast<size_t>(m_measurement_resolution.probe_index);
        if (slot < probe_ids.size()) {
            probe_source = probe_ids[slot];
            have_probe_source = true;
        }
    }
    const bool source_changed =
        !m_measure_source_known || all_visible_source != m_last_measure_all_visible ||
        (!all_visible_source && have_probe_source && probe_source != m_last_measure_probe);
    if (source_changed) {
        m_markers.clearPeakCaches();
        m_measure_source_known = true;
        m_last_measure_all_visible = all_visible_source;
        m_last_measure_probe = probe_source;
    }

    if (active_nodes.empty()) {
        ImGui::Text("No signal node selected for display!");
        ImGui::End();
        return;
    }

    const std::vector<double> *freq_axis = nullptr;
    for (auto *node : active_nodes) {
        if (!node)
            continue;
        const auto &spec = outputSpectrumFor(node);
        if (!freq_axis && !spec.frequencies.empty())
            freq_axis = &spec.frequencies;
    }

    if (!freq_axis) {
        ImGui::Text("Unable to display spectrum (no frequency grid).");
        ImGui::End();
        return;
    }

    // Build combined specs from VISIBLE traces only (for marker + avg noise)
    std::vector<const Spectrum *> visible_specs;
    for (size_t i = 0; i < active_nodes.size(); ++i) {
        if (!m_trace_visible[i])
            continue;
        auto *node = active_nodes[i];
        if (!node)
            continue;
        visible_specs.push_back(&outputSpectrumFor(node));
    }

    // Prune stale trace history for nodes no longer visible
    std::vector<const Spectrum *> all_active;
    std::vector<bool> all_active_visible;
    for (size_t i = 0; i < active_nodes.size(); ++i) {
        auto *node = active_nodes[i];
        if (!node)
            continue;
        all_active.push_back(&outputSpectrumFor(node));
        all_active_visible.push_back(m_trace_visible[i]);
    }
    // A node probed at two outputs contributes only its last probed output above, and
    // active_nodes holds one entry per node. Add the selected output's spectrum so the
    // readout path and its hold history use the trace the selector names.
    if (!m_measurement_resolution.allVisible()) {
        const size_t slot = static_cast<size_t>(m_measurement_resolution.probe_index);
        if (const Spectrum *measured = spectrumForProbeSlot(slot);
            measured &&
            std::find(all_active.begin(), all_active.end(), measured) == all_active.end()) {
            all_active.push_back(measured);
            all_active_visible.push_back(true);
        }
    }
    m_engine.pruneHistory(all_active);

    // Readouts and peak searches follow the selected measurement source: all visible traces
    // (the historic combined readout) or one probe's own trace. Held values still advance for
    // every probe, so hiding or unselecting a trace does not reset its Max/Min Hold state.
    std::vector<const Spectrum *> measure_specs;
    std::vector<bool> readout_visible = all_active_visible;
    if (m_measurement_resolution.allVisible()) {
        measure_specs = visible_specs;
    } else {
        const size_t slot = static_cast<size_t>(m_measurement_resolution.probe_index);
        readout_visible.assign(all_active.size(), false);
        if (const Spectrum *measured = spectrumForProbeSlot(slot)) {
            measure_specs.push_back(measured);
            const auto match = std::find(all_active.begin(), all_active.end(), measured);
            if (match != all_active.end())
                readout_visible[static_cast<size_t>(match - all_active.begin())] = true;
        }
    }

    // Combined trace and its marker/peak axis share the common readout axis.
    m_readout_axis = m_engine.combinedReadoutAxis(measure_specs);
    freq_axis = &m_readout_axis;
    // Marker and peak readouts use the jitter-free trace on the same axis (issue #292). Every
    // active spectrum is passed so hidden traces keep their held value (issue #291).
    m_readout_dBm = m_engine.renderCombinedReadoutSpectrum(all_active, readout_visible);
    // The peak table is the strongest part of the same readout trace, so it follows the
    // measurement source, trace visibility, and the hold/average mode exactly as Peak does.
    m_peaks.setPeaks(m_engine.findPeaks(m_readout_dBm, *freq_axis, PeakTableModel::kMaxPeaks));
    // The harmonic measurement reads the same trace as Peak and the markers; when the user
    // asked for it, the selected marker's frequency is taken as the fundamental.
    std::optional<double> harmonic_fundamental;
    if (m_harmonics.useMarker() && m_markers.enabled(m_markers.selected()))
        harmonic_fundamental = m_markers.frequency(m_markers.selected());
    m_harmonics.measure(*freq_axis, m_readout_dBm, m_engine.startFrequency(),
                        m_engine.stopFrequency(), m_engine.rbw(), harmonic_fundamental);
    const std::vector<double> &readout_dBm = m_readout_dBm;
    if (readout_dBm.empty()) {
        // The plot loop below is skipped here; advance each plotted hold so it matches the readout.
        for (auto *node : active_nodes) {
            if (node)
                m_engine.renderSpectrum(outputSpectrumFor(node));
        }
        if (m_measurement_resolution.allVisible())
            ImGui::Text("No visible traces. Enable at least one trace in the legend.");
        else
            ImGui::Text("The measured probe has no frequency grid.");
        ImGui::End();
        return;
    }

    static const ImVec4 trace_colors[4] = {
        ImVec4(0.09f, 0.78f, 0.60f, 1.0f), // Teal
        ImVec4(0.90f, 0.59f, 0.16f, 1.0f), // Orange
        ImVec4(0.47f, 0.20f, 0.67f, 1.0f), // Purple
        ImVec4(0.24f, 0.55f, 0.86f, 1.0f), // Blue
    };
    static const ImVec4 pfb_full_color(0.60f, 0.80f, 0.95f, 1.0f);      // Light sky blue
    static const ImVec4 pfb_highlight_color(1.00f, 0.50f, 0.00f, 1.0f); // Bright orange

    ImPlot::SetNextAxesLimits(m_engine.startFrequency(), m_engine.stopFrequency(),
                              m_engine.minPower(), m_engine.maxPower(), ImPlotCond_Always);

    // Let the plot absorb the remaining window height (ImPlot's own default is
    // a fixed 300 px), reserving room for the readout rows drawn below it and
    // never dropping under the usable floor (issue #88).
    const float bottom_reserve = kPlotBottomReserve + markerAreaReserve();
    const float plot_height =
        std::max(kMinPlotHeight, ImGui::GetContentRegionAvail().y - bottom_reserve);

    if (ImPlot::BeginPlot("Spectrum", ImVec2(-1.0f, plot_height))) {
        const ImVec2 plot_pos = ImPlot::GetPlotPos();
        const ImVec2 plot_size = ImPlot::GetPlotSize();
        m_last_plot_area = {plot_pos.x, plot_pos.y, plot_size.x, plot_size.y};

        for (size_t i = 0; i < active_nodes.size(); ++i) {
            auto *node = active_nodes[i];
            if (!node)
                continue;
            const auto &spec = outputSpectrumFor(node);
            auto pfbIter = m_pfb_map.find(node);
            bool is_pfb = pfbIter != m_pfb_map.end();
            // The "Active Ch" highlight only makes sense on the PFB full-band
            // output (OUT2 / outputs[1]); the channelized OUT1 trace already
            // is the active channel.
            bool is_pfb_full = is_pfb && &spec == &node->outputs[1];

            std::vector<double> trace = m_engine.renderSpectrum(spec);
            if (trace.size() != spec.frequencies.size())
                continue;

            std::string label =
                (i < m_probe_labels.size()) ? m_probe_labels[i] : ("Probe " + std::to_string(i));

            // Sync ImPlot visibility with our tracking
            ImPlot::HideNextItem(!m_trace_visible[i], ImPlotCond_Always);
            ImPlot::PlotLine(label.c_str(), spec.frequencies.data(), trace.data(),
                             static_cast<int>(trace.size()),
                             {ImPlotProp_LineColor, is_pfb ? pfb_full_color : trace_colors[i % 4],
                              ImPlotProp_LineWeight, is_pfb ? 1.0f : 1.5f});

            // Detect legend click to toggle visibility
            if (ImPlot::IsLegendEntryHovered(label.c_str()) &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_trace_visible[i] = !m_trace_visible[i];
            }

            // For PFB full-band output: overlay active channel highlight trace
            if (is_pfb_full) {
                const auto *pfb_ptr = pfbIter->second;
                double ch_center = pfb_ptr->activeChannelCenter_Hz();
                double ch_bw = pfb_ptr->activeChannelBandwidth_Hz();
                double ch_lo = ch_center - ch_bw / 2.0;
                double ch_hi = ch_center + ch_bw / 2.0;

                std::vector<double> highlight_freqs;
                std::vector<double> highlight_data;
                for (size_t j = 0; j < trace.size() && j < spec.frequencies.size(); ++j) {
                    if (spec.frequencies[j] >= ch_lo && spec.frequencies[j] <= ch_hi) {
                        highlight_freqs.push_back(spec.frequencies[j]);
                        highlight_data.push_back(trace[j]);
                    }
                }
                if (!highlight_data.empty()) {
                    ImPlot::PlotLine(
                        "Active Ch", highlight_freqs.data(), highlight_data.data(),
                        static_cast<int>(highlight_data.size()),
                        {ImPlotProp_LineColor, pfb_highlight_color, ImPlotProp_LineWeight, 2.5f});
                }
            }
        }

        // Markers overlay the traces; a grabbed marker line must not also
        // start the drag-to-zoom band.
        const bool marker_tool_active = drawMarkersOnPlot(*freq_axis, readout_dBm);

        // Drag-to-zoom, and Ctrl+click to place the selected marker.
        if (ImPlot::IsPlotHovered() && !marker_tool_active) {
            const bool ctrl = ImGui::GetIO().KeyCtrl;
            if (ctrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImPlotPoint p = ImPlot::GetPlotMousePos();
                const double placed =
                    std::clamp(p.x, m_engine.startFrequency(), m_engine.stopFrequency());
                const int marker = m_markers.selected();
                if (!m_markers.enabled(marker))
                    m_markers.setEnabled(marker, true);
                m_markers.place(marker, placed);
            } else if (!ctrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImPlotPoint p = ImPlot::GetPlotMousePos();
                m_zoom.active = true;
                m_zoom.start_freq_Hz = p.x;
                m_zoom.end_freq_Hz = p.x;
            }
            if (m_zoom.active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                ImPlotPoint p = ImPlot::GetPlotMousePos();
                m_zoom.end_freq_Hz = p.x;
            }
        }

        // Draw selection rectangle during drag
        if (m_zoom.active) {
            double x1 = m_zoom.start_freq_Hz;
            double x2 = m_zoom.end_freq_Hz;
            if (std::abs(x2 - x1) > 1.0) {
                double y1 = m_engine.minPower();
                double y2 = m_engine.maxPower();
                ImPlotPoint p1 = ImPlot::PlotToPixels(x1, y1);
                ImPlotPoint p2 = ImPlot::PlotToPixels(x2, y2);
                ImPlot::GetPlotDrawList()->AddRectFilled(ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y),
                                                         IM_COL32(100, 150, 255, 40));
            }
        }

        ImPlot::EndPlot();
    }

    // Apply zoom on release
    if (m_zoom.active && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_zoom.active = false;
        double f1 = m_zoom.start_freq_Hz;
        double f2 = m_zoom.end_freq_Hz;
        double lo = std::min(f1, f2);
        double hi = std::max(f1, f2);
        if (hi - lo > 1.0) {
            m_engine.setStartFrequency(lo);
            m_engine.setStopFrequency(hi);
        }
    }

    double avg_noise = m_engine.computeAverageNoiseLevel(measure_specs);
    ImGui::Text("Average noise level: %.2f dBm", avg_noise);

    // Trace mode + peak readout
    {
        const char *mode_name = "Clear/Write";
        switch (m_engine.traceMode()) {
        case TraceMode::MaxHold:
            mode_name = "Max Hold";
            break;
        case TraceMode::MinHold:
            mode_name = "Min Hold";
            break;
        case TraceMode::VideoAverage:
            mode_name = "Video Avg";
            break;
        default:
            break;
        }
        // Find peak in combined display
        double peak_val = -174.0;
        double peak_freq = 0.0;
        if (!readout_dBm.empty() && !freq_axis->empty()) {
            for (size_t i = 0; i < readout_dBm.size() && i < freq_axis->size(); ++i) {
                if (readout_dBm[i] > peak_val) {
                    peak_val = readout_dBm[i];
                    peak_freq = (*freq_axis)[i];
                }
            }
        }
        ImGui::Text("Trace: %s | Peak: %.2f MHz, %.2f dBm", mode_name, peak_freq / 1e6, peak_val);
    }

    if (ImGui::Button("Reset Zoom")) {
        m_engine.setStartFrequency(MIN_FREQ);
        m_engine.setStopFrequency(MAX_FREQ);
    }

    drawMarkerControls(*freq_axis, readout_dBm);
    drawMarkerTable(*freq_axis, readout_dBm);
    drawPeakTable();
    drawHarmonicTable();

    ImGui::End();
}
