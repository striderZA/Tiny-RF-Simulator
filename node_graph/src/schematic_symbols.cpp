#include "imgui.h"
#include "node_graph_widget.h"
#include <algorithm>
#include <cmath>

// ponytail: per-name symbol helpers are static and one-shot.

// Shared design box (node_graph/AGENTS.md, Schematic symbol contract). Every glyph's strokes stay
// inside the 48x32 box centred on the 96x64 node body. Terminal leads run to the box edge at
// x = ±24. Labels sit in one slot below the box, still inside the body. Strokes are 2 px.
namespace {

constexpr float kStroke = 2.0f;
constexpr float kEdgeX = 24.0f;    // box half-width; terminal leads end here
constexpr float kLabelTop = 17.0f; // label slot, measured down from the centre
constexpr float kDotRadius = 2.5f; // terminal dot

void drawLabel(ImDrawList *dl, ImVec2 c, const char *text, ImU32 color) {
    const ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y + kLabelTop), color, text);
}

void drawDashedLine(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 color) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    const float ux = dx / length, uy = dy / length;
    constexpr float dash = 4.0f, period = 7.0f;
    for (float s = 0.0f; s < length; s += period) {
        const float e = std::min(s + dash, length);
        dl->AddLine(ImVec2(a.x + ux * s, a.y + uy * s), ImVec2(a.x + ux * e, a.y + uy * e), color,
                    kStroke);
    }
}

static void drawGeneratorSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    // Two-cycle sine, 9 px amplitude, with the output lead to the box edge.
    constexpr int N = 33;
    constexpr float W = 18.0f, A = 9.0f;
    ImVec2 pts[N];
    for (int i = 0; i < N; ++i) {
        float t = static_cast<float>(i) / (N - 1);
        float x = c.x - W + 2.0f * W * t;
        float y = c.y - A * std::sin(2.0f * 3.14159265f * 2.0f * t);
        pts[i] = ImVec2(x, y);
    }
    dl->AddPolyline(pts, N, color, ImDrawFlags_None, kStroke);
    dl->AddLine(ImVec2(c.x + W, c.y), ImVec2(c.x + kEdgeX, c.y), color, kStroke);
}

static void drawAmplifierSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    const ImVec2 a(c.x - 14.0f, c.y - 13.0f);
    const ImVec2 b(c.x - 14.0f, c.y + 13.0f);
    const ImVec2 d(c.x + 14.0f, c.y);
    dl->AddTriangle(a, b, d, color, kStroke);
    dl->AddLine(ImVec2(c.x - kEdgeX, c.y), ImVec2(c.x - 14.0f, c.y), color, kStroke);
    dl->AddLine(ImVec2(c.x + 14.0f, c.y), ImVec2(c.x + kEdgeX, c.y), color, kStroke);
    // "+" on the upper input side, "-" below it, inside the triangle.
    dl->AddLine(ImVec2(c.x - 10.0f, c.y - 5.0f), ImVec2(c.x - 4.0f, c.y - 5.0f), color, kStroke);
    dl->AddLine(ImVec2(c.x - 7.0f, c.y - 8.0f), ImVec2(c.x - 7.0f, c.y - 2.0f), color, kStroke);
    dl->AddLine(ImVec2(c.x - 10.0f, c.y + 5.0f), ImVec2(c.x - 4.0f, c.y + 5.0f), color, kStroke);
}

static void drawMixerSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    dl->AddCircle(c, 14.0f, color, 24, kStroke);
    dl->AddLine(ImVec2(c.x - kEdgeX, c.y), ImVec2(c.x - 14.0f, c.y), color, kStroke);
    dl->AddLine(ImVec2(c.x + 14.0f, c.y), ImVec2(c.x + kEdgeX, c.y), color, kStroke);
    dl->AddLine(ImVec2(c.x - 9.0f, c.y - 9.0f), ImVec2(c.x + 9.0f, c.y + 9.0f), color, kStroke);
    dl->AddLine(ImVec2(c.x - 9.0f, c.y + 9.0f), ImVec2(c.x + 9.0f, c.y - 9.0f), color, kStroke);
}

static void drawSplitterSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    ImVec2 in_pt(c.x - 21.5f, c.y);
    ImVec2 mid(c.x, c.y);
    ImVec2 out_a(c.x + 21.5f, c.y - 12.0f);
    ImVec2 out_b(c.x + 21.5f, c.y + 12.0f);
    dl->AddLine(in_pt, mid, color, kStroke);
    dl->AddLine(mid, out_a, color, kStroke);
    dl->AddLine(mid, out_b, color, kStroke);
    dl->AddCircleFilled(in_pt, kDotRadius, color);
    dl->AddCircleFilled(out_a, kDotRadius, color);
    dl->AddCircleFilled(out_b, kDotRadius, color);
}

static void drawCombinerSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    // Reverse of splitter: 2 inputs on left, 1 output on right
    ImVec2 in_a(c.x - 21.5f, c.y - 12.0f);
    ImVec2 in_b(c.x - 21.5f, c.y + 12.0f);
    ImVec2 mid(c.x, c.y);
    ImVec2 out_pt(c.x + 21.5f, c.y);
    dl->AddLine(in_a, mid, color, kStroke);
    dl->AddLine(in_b, mid, color, kStroke);
    dl->AddLine(mid, out_pt, color, kStroke);
    dl->AddCircleFilled(in_a, kDotRadius, color);
    dl->AddCircleFilled(in_b, kDotRadius, color);
    dl->AddCircleFilled(out_pt, kDotRadius, color);
}

static void drawRFSwitchSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    // SPDT: common pole on the left with an arm resting toward a throw.
    ImVec2 pole(c.x - 16.0f, c.y + 6.0f);
    ImVec2 throw_a(c.x + 14.0f, c.y - 12.0f);
    ImVec2 throw_b(c.x + 14.0f, c.y + 6.0f);
    ImVec2 arm_end(c.x + 10.0f, c.y - 3.0f);
    dl->AddCircleFilled(pole, 3.0f, color);
    dl->AddCircleFilled(throw_a, kDotRadius, color);
    dl->AddCircleFilled(throw_b, kDotRadius, color);
    dl->AddLine(pole, arm_end, color, kStroke);
    dl->AddLine(ImVec2(c.x - kEdgeX, c.y + 6.0f), pole, color, kStroke);
    dl->AddLine(throw_a, ImVec2(c.x + kEdgeX, c.y - 12.0f), color, kStroke);
    dl->AddLine(throw_b, ImVec2(c.x + kEdgeX, c.y + 6.0f), color, kStroke);
}

static void drawRFSwitch2to1Symbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    // Mirror of the 1:2 SPDT: two throw contacts on the left, common pole on
    // the right with the arm resting toward the selected throw's side.
    ImVec2 pole(c.x + 16.0f, c.y + 6.0f);
    ImVec2 throw_a(c.x - 14.0f, c.y - 12.0f);
    ImVec2 throw_b(c.x - 14.0f, c.y + 6.0f);
    ImVec2 arm_end(c.x - 10.0f, c.y - 3.0f);
    dl->AddCircleFilled(pole, 3.0f, color);
    dl->AddCircleFilled(throw_a, kDotRadius, color);
    dl->AddCircleFilled(throw_b, kDotRadius, color);
    dl->AddLine(pole, arm_end, color, kStroke);
    dl->AddLine(ImVec2(c.x + kEdgeX, c.y + 6.0f), pole, color, kStroke);
    dl->AddLine(throw_a, ImVec2(c.x - kEdgeX, c.y - 12.0f), color, kStroke);
    dl->AddLine(throw_b, ImVec2(c.x - kEdgeX, c.y + 6.0f), color, kStroke);
}

static void drawAdcSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    // Four-step quantiser staircase, 23 px tall: the size floor keeps it legible at 1x.
    ImVec2 pts[8] = {
        ImVec2(c.x - kEdgeX, c.y + 10.0f), ImVec2(c.x - 12.0f, c.y + 10.0f),
        ImVec2(c.x - 12.0f, c.y + 3.0f),   ImVec2(c.x, c.y + 3.0f),
        ImVec2(c.x, c.y - 4.0f),           ImVec2(c.x + 12.0f, c.y - 4.0f),
        ImVec2(c.x + 12.0f, c.y - 11.0f),  ImVec2(c.x + kEdgeX, c.y - 11.0f),
    };
    dl->AddPolyline(pts, 8, color, ImDrawFlags_None, kStroke);
}

static void drawFilterSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    dl->AddCircle(ImVec2(c.x - 9.0f, c.y), 5.0f, color, 16, kStroke);
    dl->AddCircle(ImVec2(c.x + 9.0f, c.y), 5.0f, color, 16, kStroke);
    dl->AddLine(ImVec2(c.x - 20.0f, c.y - 10.0f), ImVec2(c.x - 20.0f, c.y + 10.0f), color, kStroke);
    dl->AddLine(ImVec2(c.x + 20.0f, c.y - 10.0f), ImVec2(c.x + 20.0f, c.y + 10.0f), color, kStroke);
    dl->AddLine(ImVec2(c.x - kEdgeX, c.y), ImVec2(c.x + kEdgeX, c.y), color, kStroke);
}

static void drawCoaxSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    dl->AddCircle(c, 14.0f, color, 32, kStroke);
    dl->AddCircle(c, 7.0f, color, 24, kStroke);
    dl->AddLine(ImVec2(c.x - kEdgeX, c.y), ImVec2(c.x - 14.0f, c.y), color, kStroke);
    dl->AddLine(ImVec2(c.x + 14.0f, c.y), ImVec2(c.x + kEdgeX, c.y), color, kStroke);
}

static void drawEqualizerSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    // Falling slope across the box, 44 px wide and 22 px tall: the size floor keeps it legible at
    // 1x. The ends stop short of the edge so the anti-aliased stroke stays inside the box.
    dl->AddLine(ImVec2(c.x - 22.0f, c.y + 11.0f), ImVec2(c.x + 22.0f, c.y - 11.0f), color, kStroke);
    // Reference markers sit on the slope.
    dl->AddCircleFilled(ImVec2(c.x - 11.0f, c.y + 5.5f), 2.0f, color);
    dl->AddCircleFilled(ImVec2(c.x + 11.0f, c.y - 5.5f), 2.0f, color);
}

static void drawAttenuatorSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    // Zigzag resistor-style symbol between two leads, with "ATT" in the label slot.
    const float x0 = c.x - 20.0f;
    const float x1 = c.x + 20.0f;
    const float amp = 5.0f;
    dl->AddLine(ImVec2(c.x - kEdgeX, c.y), ImVec2(x0, c.y), color, kStroke);
    dl->AddLine(ImVec2(x1, c.y), ImVec2(c.x + kEdgeX, c.y), color, kStroke);
    dl->PathClear();
    dl->PathLineTo(ImVec2(x0, c.y));
    for (int i = 0; i < 4; ++i) {
        float x = x0 + (x1 - x0) * (i + 0.5f) / 4.0f;
        float yo = (i % 2 == 0) ? -amp : amp;
        dl->PathLineTo(ImVec2(x, c.y + yo));
    }
    dl->PathLineTo(ImVec2(x1, c.y));
    dl->PathStroke(color, 0, kStroke);
    drawLabel(dl, c, "ATT", color);
}

static void drawPfbSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    // Three stacked channel filters, 40x24 px, with the input and output leads at the centre row.
    for (int i = 0; i < 3; ++i) {
        float y_off = (i - 1) * 8.0f;
        dl->AddRect(ImVec2(c.x - 20.0f, c.y - 4.0f + y_off),
                    ImVec2(c.x + 20.0f, c.y + 4.0f + y_off), color, 0.0f, ImDrawFlags_None,
                    kStroke);
    }
    dl->AddLine(ImVec2(c.x - kEdgeX, c.y), ImVec2(c.x - 20.0f, c.y), color, kStroke);
    dl->AddLine(ImVec2(c.x + 20.0f, c.y), ImVec2(c.x + kEdgeX, c.y), color, kStroke);
    drawLabel(dl, c, "M", color);
}

static void drawGroupCollapsedSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    ImVec2 a(c.x - 18.0f, c.y - 6.0f);
    ImVec2 b(c.x, c.y);
    ImVec2 d(c.x + 18.0f, c.y + 6.0f);
    dl->AddLine(a, b, color, kStroke);
    dl->AddLine(b, d, color, kStroke);
    dl->AddCircleFilled(a, 3.0f, color);
    dl->AddCircleFilled(b, 3.0f, color);
    dl->AddCircleFilled(d, 3.0f, color);
}

// Unknown kind: a dashed frame with a vector "?", drawn at half alpha so it reads as "not known".
static void drawUnknownSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    const ImU32 dim = (color & 0x00FFFFFFu) | (0x80u << 24);
    const float fx = 19.0f, fy = 13.0f;
    const ImVec2 tl(c.x - fx, c.y - fy), tr(c.x + fx, c.y - fy);
    const ImVec2 bl(c.x - fx, c.y + fy), br(c.x + fx, c.y + fy);
    drawDashedLine(dl, tl, tr, dim);
    drawDashedLine(dl, tr, br, dim);
    drawDashedLine(dl, br, bl, dim);
    drawDashedLine(dl, bl, tl, dim);
    // The "?" hook: an arc from upper-left over the top, down the right, ending lower-left of
    // centre, then a short stem and a dot.
    const ImVec2 hook_c(c.x, c.y - 4.0f);
    constexpr float r = 5.0f;
    ImVec2 pts[14];
    constexpr int arc_n = 13;
    for (int i = 0; i < arc_n; ++i) {
        const float deg = 160.0f + (-50.0f - 160.0f) * static_cast<float>(i) / (arc_n - 1);
        const float rad = deg * 3.14159265f / 180.0f;
        pts[i] = ImVec2(hook_c.x + r * std::cos(rad), hook_c.y - r * std::sin(rad));
    }
    pts[arc_n] = ImVec2(c.x, c.y + 1.0f);
    dl->AddPolyline(pts, arc_n + 1, dim, ImDrawFlags_None, kStroke);
    dl->AddCircleFilled(ImVec2(c.x, c.y + 5.0f), 1.5f, dim);
}

} // namespace

void NodeGraphWidget::drawSchematicSymbol(ImDrawList *dl, ImVec2 center, NodeKind kind,
                                          ImU32 color) {
    switch (kind) {
    case NodeKind::Generator:
        drawGeneratorSymbol(dl, center, color);
        break;
    case NodeKind::Amplifier:
        drawAmplifierSymbol(dl, center, color);
        break;
    case NodeKind::Splitter:
        drawSplitterSymbol(dl, center, color);
        break;
    case NodeKind::Mixer:
        drawMixerSymbol(dl, center, color);
        break;
    case NodeKind::Adc:
        drawAdcSymbol(dl, center, color);
        break;
    case NodeKind::PFB:
        drawPfbSymbol(dl, center, color);
        break;
    case NodeKind::IdealFilter:
        drawFilterSymbol(dl, center, color);
        break;
    case NodeKind::CoaxCable:
        drawCoaxSymbol(dl, center, color);
        break;
    case NodeKind::Equalizer:
        drawEqualizerSymbol(dl, center, color);
        break;
    case NodeKind::Attenuator:
        drawAttenuatorSymbol(dl, center, color);
        break;
    case NodeKind::Combiner:
        drawCombinerSymbol(dl, center, color);
        break;
    case NodeKind::RFSwitchSPDT:
        drawRFSwitchSymbol(dl, center, color);
        break;
    case NodeKind::RFSwitchSPDT2to1:
        drawRFSwitch2to1Symbol(dl, center, color);
        break;
    case NodeKind::GroupCollapsed:
        drawGroupCollapsedSymbol(dl, center, color);
        break;
    case NodeKind::Unknown:
    default:
        // Unknown and any kind without a glyph: the dashed "?" glyph.
        drawUnknownSymbol(dl, center, color);
        break;
    }
}
