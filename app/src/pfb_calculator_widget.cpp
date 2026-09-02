#include "pfb_calculator_widget.h"
#include "component_registry.h"
#include "imgui.h"
#include "imnodes.h"
#include "pfb_channelizer_engine.h"
#include "pfb_filter_design.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
ImVec4 statusColor(RejectionStatus s) {
    switch (s) {
    case RejectionStatus::Meets:
        return ImVec4(0.35f, 1.0f, 0.35f, 1.0f);
    case RejectionStatus::Within10Db:
        return ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
    case RejectionStatus::Misses:
    default:
        return ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
    }
}
} // namespace

PfbCalculatorWidget::PfbCalculatorWidget(ComponentRegistry &components)
    : m_components(&components) {}

PFBChannelizerEngine *
PfbCalculatorWidget::resolveTarget(const std::vector<PFBChannelizerEngine *> &pfbs) {
    if (pfbs.empty()) {
        m_target_index = -1;
        return nullptr;
    }
    // Explicit combo choice wins.
    if (m_target_index >= 0 && m_target_index < static_cast<int>(pfbs.size()))
        return pfbs[m_target_index];
    m_target_index = -1;
    // Auto: a single graph-selected node that is a PFB.
    if (ImNodes::NumSelectedNodes() == 1) {
        int selected_id = -1;
        ImNodes::GetSelectedNodes(&selected_id);
        auto *engine = m_components->find(selected_id);
        if (engine && engine->type_name() == "pfb") {
            for (auto *p : pfbs)
                if (p->id() == selected_id)
                    return p;
        }
    }
    return pfbs.front();
}

void PfbCalculatorWidget::pullFrom(PFBChannelizerEngine &pfb) {
    m_M = pfb.channelCount();
    m_K = pfb.tapsPerBranch();
    m_beta = pfb.kaiserBeta();
}

// Rebuild the (expensive) prototype synthesis, metrics, and plot samples only
// when M/K/beta actually changed; draw() then reuses the cache each frame.
void PfbCalculatorWidget::refreshDesignCache() {
    if (m_M == m_cached_M && m_K == m_cached_K && m_beta == m_cached_beta)
        return;

    const PfbFilterDesign design(m_M, m_K, m_beta);
    m_cached_design = design;
    m_cached_metrics = computePfbMetrics(design);

    // Sample |H| in dB over x in [0, 1.5].
    const int kSamples = 151;
    m_plot_db.assign(kSamples, 0.0f);
    m_plot_ymax = 5.0;
    m_plot_ymin_raw = -160.0;
    for (int i = 0; i < kSamples; ++i) {
        const double x = 1.5 * i / (kSamples - 1);
        const double v = 20.0 * std::log10(std::max(design.responseAt(x), 1e-300));
        m_plot_db[i] = static_cast<float>(v);
        m_plot_ymin_raw = std::min(m_plot_ymin_raw, v);
    }
    // The cache holds ONLY the raw sampled floor: the two target-dependent
    // clamps run per frame in draw() against the live m_target_db, so dragging
    // the target slider re-floors the plot without re-sampling the design.

    m_cached_M = m_M;
    m_cached_K = m_K;
    m_cached_beta = m_beta;
}

void PfbCalculatorWidget::draw(const char *title, bool *p_open) {
    ImGui::SetNextWindowSize(ImVec2(820, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    auto pfbs = m_components->byType<PFBChannelizerEngine>();
    PFBChannelizerEngine *target = resolveTarget(pfbs);

    // Pull-on-retarget: whenever the bound target changes, load its current
    // M/K/beta so the panel shows reality, not last-edited values.
    const int target_id = target ? target->id() : -1;
    if (target_id != m_bound_pfb_id) {
        if (target)
            pullFrom(*target);
        m_bound_pfb_id = target_id;
    }
    if (!target)
        m_bound_pfb_id = -1;

    // Refresh the design/metric cache up front (no-op while M/K/beta are
    // unchanged) so both columns can read one set of readouts.
    refreshDesignCache();
    const RejectionStatus st =
        compareRejection(m_cached_metrics.adjacent_rejection_db, m_target_db);
    const std::string hint = pfbGuidanceText(m_cached_design, m_cached_metrics, m_target_db);

    // --- Left column: controls -------------------------------------------
    ImGui::BeginGroup();
    ImGui::BeginChild("##calc_controls", ImVec2(300, 0), false);

    std::string preview = "Auto (graph selection)";
    if (m_target_index >= 0 && m_target_index < static_cast<int>(pfbs.size()))
        preview = "PFB " + std::to_string(pfbs[m_target_index]->id());
    if (ImGui::BeginCombo("Target PFB", preview.c_str())) {
        if (ImGui::Selectable("Auto (graph selection)", m_target_index < 0))
            m_target_index = -1;
        for (int i = 0; i < static_cast<int>(pfbs.size()); ++i) {
            const bool selected = (m_target_index == i);
            const std::string label = "PFB " + std::to_string(pfbs[i]->id());
            if (ImGui::Selectable(label.c_str(), selected))
                m_target_index = i;
        }
        ImGui::EndCombo();
    }
    ImGui::Separator();

    if (ImGui::InputInt("Channels M", &m_M, 1, 100))
        m_M = std::clamp(m_M, 2, 2048);
    ImGui::SliderInt("Taps/branch K", &m_K, 1, 64);
    ImGui::SliderFloat("Kaiser beta", &m_beta, 0.0f, 20.0f, "%.2f");
    ImGui::SliderFloat("Target rejection (dB)", &m_target_db, 20.0f, 140.0f, "%.0f");

    if (target) {
        if (ImGui::Button("Apply to PFB")) {
            target->setChannelCount(m_M);
            target->setTapsPerBranch(m_K);
            target->setKaiserBeta(m_beta);
            if (onParamChange)
                onParamChange();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("writes M/K/beta to PFB %d", target->id());
        if (target->fs_Hz() > 0.0)
            ImGui::Text("Input Fs: %.3f MHz", target->fs_Hz() / 1e6);
        else
            ImGui::TextDisabled("PFB has no input yet (Fs unknown)");
    } else {
        ImGui::TextDisabled("Design only: add a PFB to the graph to enable Apply.");
    }

    // The guidance hint lives here (left column) so it costs the plot no
    // vertical space; it can wrap to several lines without squeezing the plot.
    if (!hint.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("Hint: %s", hint.c_str());
    }
    ImGui::EndChild();
    ImGui::EndGroup();

    // --- Right column: plot + metrics -------------------------------------
    ImGui::SameLine();
    ImGui::BeginChild("##calc_analysis", ImVec2(0, 0), true);

    // Per-frame floor: the target slider mutates m_target_db during a drag, so
    // the two target-dependent y clamps run every frame against the live value
    // (O(1) arithmetic, no resample); m_plot_ymin_raw is the cached raw floor.
    const double target_db = std::max(0.0, static_cast<double>(m_target_db));
    double y_min = std::max(m_plot_ymin_raw, -std::max(target_db + 12.0, 60.0));
    y_min = std::min(y_min, -target_db - 4.0);

    // The plot owns most of the child height; readouts below are compact
    // two-per-row lines (the guidance hint lives in the left column).
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float plot_h = std::max(avail.y - 116.0f, 100.0f);
    drawPlot(ImGui::GetWindowDrawList(), origin, avail.x, plot_h, m_plot_db, y_min, m_plot_ymax);
    ImGui::Dummy(ImVec2(avail.x, plot_h));

    ImGui::Separator();
    ImGui::TextColored(statusColor(st), "Adjacent rejection: %.1f dB (target %.0f dB)",
                       -m_cached_metrics.adjacent_rejection_db, m_target_db);

    ImGui::Text("-3 dB half-width:  %.3f channel", m_cached_metrics.passband_halfwidth_ch);
    ImGui::SameLine(250.0f);
    ImGui::Text("Band-edge loss: %.1f dB", m_cached_metrics.edge_loss_db);

    ImGui::Text("Far floor (x in 1.0..1.5): %.1f dB", m_cached_metrics.far_floor_db);
    ImGui::SameLine(250.0f);
    ImGui::Text("Taps N = M*K: %d", m_cached_metrics.total_taps);

    ImGui::Text("Flat-noise tilt: %.2f dB", m_cached_metrics.flat_noise_tilt_db);

    if (hint.empty())
        ImGui::TextColored(statusColor(st), "Rejection target met.");
    ImGui::EndChild();

    ImGui::End();
}

void PfbCalculatorWidget::drawPlot(ImDrawList *dl, const ImVec2 &origin, float w, float h,
                                   const std::vector<float> &db, double y_min, double y_max) {
    const float kTop = 8.0f, kBottom = 20.0f, kLeft = 12.0f, kRight = 8.0f;
    const int n = static_cast<int>(db.size());
    if (w <= kLeft + kRight || h <= kTop + kBottom || n < 2)
        return;

    auto x_px = [&](double x) {
        return origin.x + kLeft + static_cast<float>((x / 1.5) * (w - kLeft - kRight));
    };
    auto y_px = [&](double v) {
        const double span = y_max - y_min;
        const float f = static_cast<float>((v - y_min) / span);
        return origin.y + kTop + (1.0f - f) * (h - kTop - kBottom);
    };

    // Grid + markers: channel edge x=0.5, adjacent center x=1.0.
    const unsigned grid_col = IM_COL32(120, 120, 120, 120);
    const unsigned edge_col = IM_COL32(200, 200, 80, 160);
    const unsigned target_col = IM_COL32(255, 90, 220, 200);
    const unsigned curve_col = IM_COL32(110, 200, 255, 255);
    dl->AddLine(ImVec2(x_px(0.0), y_px(0.0)), ImVec2(x_px(1.5), y_px(0.0)), grid_col);
    dl->AddLine(ImVec2(x_px(0.5), origin.y + kTop), ImVec2(x_px(0.5), origin.y + h - kBottom),
                edge_col);
    dl->AddLine(ImVec2(x_px(1.0), origin.y + kTop), ImVec2(x_px(1.0), origin.y + h - kBottom),
                edge_col);
    dl->AddLine(ImVec2(x_px(0.0), y_px(-m_target_db)), ImVec2(x_px(1.5), y_px(-m_target_db)),
                target_col);
    dl->AddText(ImVec2(x_px(0.5) - 10.0f, origin.y + h - kBottom + 2.0f), edge_col, "0.5 edge");
    dl->AddText(ImVec2(x_px(1.0) - 10.0f, origin.y + h - kBottom + 2.0f), edge_col, "1.0 adj");

    std::vector<ImVec2> pts;
    pts.reserve(n);
    // Clip the curve to the plot box: stopband nulls fall below the y-floor
    // and would otherwise paint over the Separator/metrics drawn after.
    dl->PushClipRect(ImVec2(origin.x + kLeft, origin.y + kTop),
                     ImVec2(origin.x + w - kRight, origin.y + h - kBottom), true);
    for (int i = 0; i < n; ++i)
        pts.emplace_back(x_px(1.5 * i / (n - 1)), y_px(db[i]));
    dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), curve_col, 0, 1.6f);
    dl->PopClipRect();
}
