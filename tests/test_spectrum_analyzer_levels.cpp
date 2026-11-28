// Headless-frame coverage for the Spectrum Analyzer level fields: the +60 dBm Ref
// ceiling and unchanged Min range, the Ref > Min invariant, the rendered 60 dBm
// axis, and the shared MAX_POWER caps on tone amplitudes and the receiver
// requirements input level. Out-of-range fields are observed through the
// once-per-value warning inputDouble logs.
#include "circuit_runtime.h"
#include "common.h"
#include "editor_commands.h"
#include "graph_editor_actions.h"
#include "imgui.h"
#include "implot.h"
#include "inspector_panel.h"
#include "logging_core.h"
#include "node_graph_engine.h"
#include "signal_generator_engine.h"
#include "signal_generator_widget.h"
#include "spectrum_analyzer_widget.h"
#include "utils.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <functional>
#include <string>

namespace {

struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char *atlas_pixels = nullptr;
        int atlas_w = 0;
        int atlas_h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&atlas_pixels, &atlas_w, &atlas_h);
    }
    ~ImGuiFixture() {
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

std::size_t warningsAbout(const std::string &field) {
    std::size_t count = 0;
    for (const LogEntry &entry : LoggerCore::instance().entries())
        if (entry.level == Level::Warn && entry.message.find(field) != std::string::npos)
            ++count;
    return count;
}

void drawAnalyzerFrame(SpectrumAnalyzerWidget &widget) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(640.0f, 700.0f), ImGuiCond_Always);
    widget.draw("Spectrum Analyzer");
    ImGui::Render();
}

// One frame whose body draws its own window (a widget's draw()).
void drawOwnWindowFrame(const std::function<void()> &body) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f), ImGuiCond_Always);
    body();
    ImGui::Render();
}

// One frame drawing `body` inside a window this helper owns.
void drawBodyFrame(const char *title, const std::function<void()> &body) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f), ImGuiCond_Always);
    ImGui::Begin(title);
    body();
    ImGui::End();
    ImGui::Render();
}

// One active node with a small frequency grid, so draw() reaches the plot.
struct AnalyzerFixture : ImGuiFixture {
    AnalyzerFixture() {
        m_node.view_enabled = true;
        Spectrum &out = m_node.outputs.emplace_back();
        out.frequencies = {0.0, 10e6, 20e6};
        out.noise_W.assign(3, 1e-12);
        out.computeTotalNoise();
        m_view.registerNode(&m_node);
    }
    ViewManager m_view;
    SignalNode m_node;
    SpectrumAnalyzerEngine m_engine;
    SpectrumAnalyzerWidget m_widget{m_engine, m_view};
};

} // namespace

TEST_CASE("the analyzer reference ceiling is +60 dBm and the shared cap stays +10 dBm",
          "[spectrum_analyzer][levels]") {
    SpectrumAnalyzerEngine engine;
    CHECK(MAX_REF_POWER_DBM == 60.0);
    CHECK(engine.maxPower() == MAX_REF_POWER_DBM);
    CHECK(engine.minPower() == MIN_POWER);
    CHECK(MAX_POWER == 10.0);
    CHECK(MIN_POWER == -174.0);

    CHECK(utils::boundedEdit(10.0, 60.0, MIN_POWER, MAX_REF_POWER_DBM) == 60.0);
    CHECK(utils::boundedEdit(10.0, 61.0, MIN_POWER, MAX_REF_POWER_DBM) == 60.0);
    CHECK_FALSE(utils::boundedEdit(60.0, 61.0, MIN_POWER, MAX_REF_POWER_DBM).has_value());

    // Ref must stay strictly above Min: equal or lower inverts or blanks the axis.
    CHECK(levelAxisValid(60.0, -174.0));
    CHECK(levelAxisValid(0.0, -1.0));
    CHECK_FALSE(levelAxisValid(0.0, 0.0));
    CHECK_FALSE(levelAxisValid(-10.0, 0.0));
}

TEST_CASE_METHOD(ImGuiFixture, "the Ref field reaches 60 dBm and flags 61",
                 "[spectrum_analyzer][levels]") {
    ViewManager view;
    SpectrumAnalyzerEngine engine;
    SpectrumAnalyzerWidget widget(engine, view);

    engine.setMaxPower(MAX_REF_POWER_DBM);
    const std::size_t at_ceiling = warningsAbout("Ref (dBm):");
    drawAnalyzerFrame(widget);
    CHECK(warningsAbout("Ref (dBm):") == at_ceiling);

    // Above the ceiling the value is shown as is (flagged), not silently rewritten.
    engine.setMaxPower(MAX_REF_POWER_DBM + 1.0);
    const std::size_t above = warningsAbout("Ref (dBm):");
    drawAnalyzerFrame(widget);
    CHECK(engine.maxPower() == MAX_REF_POWER_DBM + 1.0);
    CHECK(warningsAbout("Ref (dBm):") == above + 1);
}

TEST_CASE_METHOD(ImGuiFixture, "the Min level field keeps -174..+10 dBm",
                 "[spectrum_analyzer][levels]") {
    ViewManager view;
    SpectrumAnalyzerEngine engine;
    SpectrumAnalyzerWidget widget(engine, view);

    engine.setMinPower(MAX_POWER);
    const std::size_t at_ceiling = warningsAbout("Min level (dBm):");
    drawAnalyzerFrame(widget);
    CHECK(warningsAbout("Min level (dBm):") == at_ceiling);

    engine.setMinPower(MAX_POWER + 1.0);
    const std::size_t above = warningsAbout("Min level (dBm):");
    drawAnalyzerFrame(widget);
    CHECK(warningsAbout("Min level (dBm):") == above + 1);

    engine.setMinPower(MIN_POWER);
    const std::size_t at_floor = warningsAbout("Min level (dBm):");
    drawAnalyzerFrame(widget);
    CHECK(warningsAbout("Min level (dBm):") == at_floor);

    engine.setMinPower(MIN_POWER - 1.0);
    const std::size_t below = warningsAbout("Min level (dBm):");
    drawAnalyzerFrame(widget);
    CHECK(warningsAbout("Min level (dBm):") == below + 1);
}

TEST_CASE_METHOD(AnalyzerFixture, "a 60 dBm reference reaches the rendered plot axis",
                 "[spectrum_analyzer][levels]") {
    m_engine.setMaxPower(MAX_REF_POWER_DBM);
    m_engine.setMinPower(MIN_POWER);
    drawAnalyzerFrame(m_widget);

    const auto axis = m_widget.lastYAxisLimits();
    CHECK(axis.upper == MAX_REF_POWER_DBM);
    CHECK(axis.lower == MIN_POWER);
    CHECK(levelAxisValid(axis.upper, axis.lower));
}

TEST_CASE_METHOD(ImGuiFixture, "tone amplitude fields still cap at MAX_POWER",
                 "[spectrum_analyzer][levels]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine generator(1, graph);
    generator.addTone(100e6, MAX_REF_POWER_DBM);
    SignalGeneratorWidget generator_widget(generator);

    const std::size_t before_generator = warningsAbout("amp:");
    drawOwnWindowFrame([&] { generator_widget.draw("Signal Generator"); });
    CHECK(warningsAbout("amp:") == before_generator + 1);

    CircuitRuntime runtime;
    GraphEditorActions actions{runtime};
    EditorCommands commands{runtime, actions};
    InspectorPanel panel(runtime.graph(), runtime.components(), commands);
    const std::size_t before_inspector = warningsAbout("amp:");
    drawBodyFrame("Inspector", [&] { panel.drawGeneratorProperties(generator, 0); });
    CHECK(warningsAbout("amp:") == before_inspector + 1);
}
