#define private public
#include "pfb_calculator_widget.h"
#undef private

#include "component_registry.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "pfb_channelizer_engine.h"
#include <catch2/catch_test_macros.hpp>

struct PfbCalculatorImGuiFixture {
    PfbCalculatorImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
        ImGui::GetIO().DisplaySize = ImVec2(1920, 1080);
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char *atlas_pixels = nullptr;
        int atlas_w = 0;
        int atlas_h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&atlas_pixels, &atlas_w, &atlas_h);
    }
    ~PfbCalculatorImGuiFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

TEST_CASE_METHOD(PfbCalculatorImGuiFixture,
                 "PFB calculator auto-target follows selected graph node", "[pfb_calculator]") {
    NodeGraphEngine graph;
    ViewManager views;
    ComponentRegistry components(graph, views);
    auto &first = components.add<PFBChannelizerEngine>(100, graph);
    auto &second = components.add<PFBChannelizerEngine>(101, graph);
    PfbCalculatorWidget calculator(components);

    ImGui::NewFrame();
    ImNodes::BeginNodeEditor();
    ImNodes::BeginNode(first.graphNodeId());
    ImNodes::EndNode();
    ImNodes::BeginNode(second.graphNodeId());
    ImNodes::EndNode();
    ImNodes::EndNodeEditor();
    ImGui::Render();
    ImNodes::SelectNode(second.graphNodeId());
    auto pfbs = components.byType<PFBChannelizerEngine>();
    REQUIRE(calculator.resolveTarget(pfbs) == &second);
    ImNodes::ClearNodeSelection();
    (void)first;
}

TEST_CASE_METHOD(PfbCalculatorImGuiFixture, "PFB calculator explicit target survives PFB removal",
                 "[pfb_calculator]") {
    NodeGraphEngine graph;
    ViewManager views;
    ComponentRegistry components(graph, views);
    auto &first = components.add<PFBChannelizerEngine>(100, graph);
    auto &second = components.add<PFBChannelizerEngine>(101, graph);
    auto &third = components.add<PFBChannelizerEngine>(102, graph);
    PfbCalculatorWidget calculator(components);
    calculator.m_target_node_id = second.graphNodeId();

    auto pfbs = components.byType<PFBChannelizerEngine>();
    REQUIRE(calculator.resolveTarget(pfbs) == &second);

    components.remove(third.graphNodeId());
    pfbs = components.byType<PFBChannelizerEngine>();
    REQUIRE(calculator.resolveTarget(pfbs) == &second);
    (void)first;
}
