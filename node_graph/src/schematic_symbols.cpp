#include "imgui.h"
#include "node_graph_widget.h"
#include <cmath>

// ponytail: per-name symbol helpers are static and one-shot.

namespace {

static void drawGeneratorSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    constexpr int N = 16;
    constexpr float W = 30.0f, H = 12.0f;
    ImVec2 pts[N];
    for (int i = 0; i < N; ++i) {
        float t = static_cast<float>(i) / (N - 1);
        float x = c.x - W + 2.0f * W * t;
        float y = c.y - H * std::sin(2.0f * 3.14159265f * 2.0f * t);
        pts[i] = ImVec2(x, y);
    }
    dl->AddPolyline(pts, N, color, ImDrawFlags_None, 2.0f);
}

static void drawAmplifierSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    ImVec2 a(c.x - 20, c.y - 16);
    ImVec2 b(c.x - 20, c.y + 16);
    ImVec2 d(c.x + 20, c.y);
    dl->AddTriangle(a, b, d, color, 2.0f);
    dl->AddLine(ImVec2(a.x - 8, c.y - 6), ImVec2(a.x - 8, c.y - 14), color, 2.0f);
    dl->AddLine(ImVec2(a.x - 12, c.y - 10), ImVec2(a.x - 4, c.y - 10), color, 2.0f);
    dl->AddLine(ImVec2(a.x - 8, c.y + 6), ImVec2(a.x - 8, c.y + 14), color, 2.0f);
}

static void drawMixerSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    dl->AddCircle(c, 14.0f, color, 24, 2.0f);
    dl->AddLine(ImVec2(c.x - 10, c.y - 10), ImVec2(c.x + 10, c.y + 10), color, 2.0f);
    dl->AddLine(ImVec2(c.x - 10, c.y + 10), ImVec2(c.x + 10, c.y - 10), color, 2.0f);
}

static void drawSplitterSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    ImVec2 in_pt(c.x - 22, c.y);
    ImVec2 mid(c.x, c.y);
    ImVec2 out_a(c.x + 22, c.y - 12);
    ImVec2 out_b(c.x + 22, c.y + 12);
    dl->AddLine(in_pt, mid, color, 2.0f);
    dl->AddLine(mid, out_a, color, 2.0f);
    dl->AddLine(mid, out_b, color, 2.0f);
    dl->AddCircleFilled(in_pt, 2.5f, color);
    dl->AddCircleFilled(out_a, 2.5f, color);
    dl->AddCircleFilled(out_b, 2.5f, color);
}

static void drawCombinerSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    // Reverse of splitter: 2 inputs on left, 1 output on right
    ImVec2 in_a(c.x - 22, c.y - 12);
    ImVec2 in_b(c.x - 22, c.y + 12);
    ImVec2 mid(c.x, c.y);
    ImVec2 out_pt(c.x + 22, c.y);
    dl->AddLine(in_a, mid, color, 2.0f);
    dl->AddLine(in_b, mid, color, 2.0f);
    dl->AddLine(mid, out_pt, color, 2.0f);
    dl->AddCircleFilled(in_a, 2.5f, color);
    dl->AddCircleFilled(in_b, 2.5f, color);
    dl->AddCircleFilled(out_pt, 2.5f, color);
}

static void drawAdcSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    ImVec2 pts[6] = {
        ImVec2(c.x - 24, c.y + 8), ImVec2(c.x - 16, c.y + 8), ImVec2(c.x - 16, c.y - 4),
        ImVec2(c.x - 8, c.y - 4),  ImVec2(c.x - 8, c.y + 8),  ImVec2(c.x + 24, c.y + 8),
    };
    dl->AddPolyline(pts, 6, color, ImDrawFlags_None, 2.0f);
}

static void drawFilterSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    dl->AddCircle(ImVec2(c.x - 10, c.y), 5.0f, color, 16, 2.0f);
    dl->AddCircle(ImVec2(c.x + 10, c.y), 5.0f, color, 16, 2.0f);
    dl->AddLine(ImVec2(c.x - 22, c.y - 10), ImVec2(c.x - 22, c.y + 10), color, 2.0f);
    dl->AddLine(ImVec2(c.x + 22, c.y - 10), ImVec2(c.x + 22, c.y + 10), color, 2.0f);
    dl->AddLine(ImVec2(c.x - 22, c.y), ImVec2(c.x + 22, c.y), color, 2.0f);
}

static void drawCoaxSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    dl->AddCircle(c, 16.0f, color, 32, 2.0f);
    dl->AddCircle(c, 8.0f, color, 24, 2.0f);
}

static void drawEqualizerSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    // Rising/falling slope line
    dl->AddLine(ImVec2(c.x - 20, c.y + 8), ImVec2(c.x + 20, c.y - 8), color, 2.0f);
    // Small reference markers
    dl->AddCircleFilled(ImVec2(c.x - 14, c.y + 4), 2.0f, color);
    dl->AddCircleFilled(ImVec2(c.x + 14, c.y - 4), 2.0f, color);
}

static void drawAttenuatorSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    // Zigzag resistor-style symbol with "ATT" label
    const float x0 = c.x - 15.0f;
    const float x1 = c.x + 15.0f;
    const float amp = 4.0f;
    dl->PathClear();
    dl->PathLineTo(ImVec2(x0, c.y));
    for (int i = 0; i < 4; ++i) {
        float x = x0 + (x1 - x0) * (i + 0.5f) / 4.0f;
        float yo = (i % 2 == 0) ? -amp : amp;
        dl->PathLineTo(ImVec2(x, c.y + yo));
    }
    dl->PathLineTo(ImVec2(x1, c.y));
    dl->PathStroke(color, 0, 2.0f);
    const char *label = "ATT";
    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - 14.0f), color, label);
}

static void drawPfbSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    for (int i = 0; i < 3; ++i) {
        float y_off = (i - 1) * 8.0f;
        dl->AddRect(ImVec2(c.x - 20, c.y - 4 + y_off), ImVec2(c.x + 20, c.y + 4 + y_off), color,
                    0.0f, ImDrawFlags_None, 2.0f);
    }
    dl->AddText(ImVec2(c.x - 4, c.y - 18), color, "M");
}

static void drawGroupCollapsedSymbol(ImDrawList *dl, ImVec2 c, ImU32 color) {
    ImVec2 a(c.x - 18, c.y - 6);
    ImVec2 b(c.x, c.y);
    ImVec2 d(c.x + 18, c.y + 6);
    dl->AddLine(a, b, color, 2.0f);
    dl->AddLine(b, d, color, 2.0f);
    dl->AddCircleFilled(a, 3.0f, color);
    dl->AddCircleFilled(b, 3.0f, color);
    dl->AddCircleFilled(d, 3.0f, color);
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
    case NodeKind::GroupCollapsed:
        drawGroupCollapsedSymbol(dl, center, color);
        break;
    default:
        // Future/unknown kinds: draw nothing silently.
        break;
    }
}
