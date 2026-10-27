#include "imgui.h"
#include "node_graph_widget.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

// Geometry of the shared symbol design box (node_graph/AGENTS.md, Local Contracts). The box is
// 48x32 centred on the 96x64 node body. Labels sit in a slot below the box, inside the body.
namespace {

constexpr float kBoxHalfW = 24.0f;
constexpr float kBoxHalfH = 16.0f;
constexpr float kLabelTop = 17.0f;    // label slot starts below the box
constexpr float kLabelBottom = 32.0f; // body half-height
constexpr float kLabelHalfW = 12.0f;  // widest label ("ATT") fits in this half-width
constexpr float kAaSlack = 1.0f;      // ImGui anti-aliasing fringe, in px
constexpr float kMinGlyphExtent = 20.0f;

struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImGui::GetIO().DisplaySize = ImVec2(1200.0f, 800.0f);
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char *pixels = nullptr;
        int w = 0, h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    }
    ~ImGuiFixture() { ImGui::DestroyContext(); }
};

// Vertex positions of one symbol drawn at the origin, on a fresh background draw list.
std::vector<ImVec2> symbolVertices(NodeKind kind) {
    ImGui::NewFrame();
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    const int start = dl->VtxBuffer.Size;
    NodeGraphWidget::drawSchematicSymbol(dl, ImVec2(0.0f, 0.0f), kind, IM_COL32_WHITE);
    std::vector<ImVec2> points;
    for (int i = start; i < dl->VtxBuffer.Size; ++i)
        points.push_back(dl->VtxBuffer[i].pos);
    ImGui::EndFrame();
    return points;
}

struct SymbolCase {
    NodeKind kind;
    bool has_label; // draws text in the label slot
};

const std::vector<SymbolCase> &allSymbols() {
    static const std::vector<SymbolCase> cases = {
        {NodeKind::Generator, false},
        {NodeKind::Amplifier, false},
        {NodeKind::Splitter, false},
        {NodeKind::Mixer, false},
        {NodeKind::Adc, false},
        {NodeKind::PFB, true},
        {NodeKind::IdealFilter, false},
        {NodeKind::CoaxCable, false},
        {NodeKind::Equalizer, false},
        {NodeKind::Attenuator, true},
        {NodeKind::Combiner, false},
        {NodeKind::RFSwitchSPDT, false},
        {NodeKind::RFSwitchSPDT2to1, false},
        {NodeKind::GroupCollapsed, false},
        {NodeKind::Unknown, false},
    };
    return cases;
}

bool inBox(ImVec2 p) {
    return std::abs(p.x) <= kBoxHalfW + kAaSlack && std::abs(p.y) <= kBoxHalfH + kAaSlack;
}

bool inLabelSlot(ImVec2 p) {
    return std::abs(p.x) <= kLabelHalfW + kAaSlack && p.y >= kLabelTop - kAaSlack &&
           p.y <= kLabelBottom + kAaSlack;
}

} // namespace

TEST_CASE_METHOD(ImGuiFixture, "Every schematic glyph fits the shared 48x32 design box",
                 "[node_graph][symbols]") {
    for (const auto &symbol : allSymbols()) {
        INFO("kind index " << static_cast<int>(symbol.kind));
        const auto points = symbolVertices(symbol.kind);
        REQUIRE(!points.empty());
        for (const ImVec2 &p : points) {
            INFO("vertex " << p.x << ", " << p.y);
            if (symbol.has_label) {
                // Glyph strokes stay in the box; only text may sit in the label slot.
                const bool glyph_part = p.y < kLabelTop - kAaSlack;
                if (glyph_part)
                    CHECK(inBox(p));
                else
                    CHECK(inLabelSlot(p));
            } else {
                CHECK(inBox(p));
            }
        }
    }
}

TEST_CASE_METHOD(ImGuiFixture, "Small glyphs keep a size floor at 1x", "[node_graph][symbols]") {
    for (NodeKind kind : {NodeKind::Equalizer, NodeKind::Adc, NodeKind::Mixer}) {
        INFO("kind index " << static_cast<int>(kind));
        const auto points = symbolVertices(kind);
        REQUIRE(!points.empty());
        float min_x = points.front().x, max_x = points.front().x;
        float min_y = points.front().y, max_y = points.front().y;
        for (const ImVec2 &p : points) {
            min_x = std::min(min_x, p.x);
            max_x = std::max(max_x, p.x);
            min_y = std::min(min_y, p.y);
            max_y = std::max(max_y, p.y);
        }
        CHECK(max_x - min_x >= kMinGlyphExtent);
        CHECK(max_y - min_y >= kMinGlyphExtent);
    }
}

TEST_CASE_METHOD(ImGuiFixture, "Unknown kind draws a glyph distinct from every other symbol",
                 "[node_graph][symbols]") {
    const auto unknown = symbolVertices(NodeKind::Unknown);
    REQUIRE(unknown.size() > 0);
    for (const SymbolCase &symbol : allSymbols()) {
        if (symbol.kind == NodeKind::Unknown)
            continue;
        INFO("kind index " << static_cast<int>(symbol.kind));
        const auto other = symbolVertices(symbol.kind);
        bool same = other.size() == unknown.size();
        for (size_t i = 0; same && i < other.size(); ++i)
            same = other[i].x == unknown[i].x && other[i].y == unknown[i].y;
        CHECK_FALSE(same);
    }
}
