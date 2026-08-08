// Regression test for GitHub issue #37:
// "Segmentation Fault when removing PFB Channelizer input"
//
// Root cause: NodeGraphWidget::onRemoveNode (app.cpp) destroys the removed
// component's engine (and its owned SignalNode/Spectrum outputs) synchronously
// mid-frame, inside draw_ui(). Downstream components that had wired
// node().inputs[k] to point at the removed engine's Spectrum output were only
// re-wired at the *start* of update_dsp(), which does not run again until the
// next frame. Any widget that dereferences node().inputs[] directly while
// drawing later in the same frame (PFBChannelizerWidget::draw()/rebuildCache())
// therefore used a dangling pointer into freed memory -> segfault.
//
// Fix: RfSimulatorApp::onRemoveNode now calls the new rewireInputs() helper
// immediately after ComponentRegistry::remove(), so every surviving
// component's node().inputs[] reflects the current graph topology (nulled out
// for any severed source) before draw_ui() continues rendering widgets.
#include "adc_engine.h"
#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "pfb_channelizer_engine.h"
#include "signal_generator_engine.h"
#include <catch2/catch_test_macros.hpp>

struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
    }
    ~ImGuiFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

TEST_CASE_METHOD(ImGuiFixture,
                 "Removing an upstream node immediately nulls downstream dangling input "
                 "pointers (issue #37)",
                 "[app][regression][issue37]") {
    RfSimulatorApp app;
    app.newProject(); // start from an empty graph

    auto &gen = app.testComponents().add<SignalGeneratorEngine>(10001, app.testGraphEngine());
    auto &adc = app.testComponents().add<AdcEngine>(10002, app.testGraphEngine());
    auto &pfb = app.testComponents().add<PFBChannelizerEngine>(10003, app.testGraphEngine());

    app.testGraphEngine().addLink(gen.outputPinId(), adc.inputPinId());
    app.testGraphEngine().addLink(adc.outputPinId(), pfb.inputPinId());

    // Normal frame: wires PFB's input to the ADC's output Spectrum.
    app.update_dsp();
    REQUIRE(pfb.node().inputs[0] == &adc.node().outputs[0]);

    int adc_graph_id = adc.graphNodeId();
    REQUIRE(app.testGraphWidget().onRemoveNode);

    // Simulate the user deleting the ADC node mid-frame (as the node graph
    // widget's context menu / Delete key / InspectorPanel "Delete" button do),
    // which synchronously destroys the AdcEngine and its owned SignalNode.
    app.testGraphWidget().onRemoveNode(adc_graph_id);

    // The ADC engine (and the Spectrum object pfb.node().inputs[0] pointed at)
    // has now been freed. Without the fix this pointer would still be
    // dangling until the *next* update_dsp() call. The fix must null it out
    // synchronously so any widget drawn later in this same frame
    // (PFBChannelizerWidget, InspectorPanel) never dereferences freed memory.
    REQUIRE(pfb.node().inputs[0] == nullptr);

    // Sanity: a normal update_dsp() afterwards should not crash and should
    // reflect the severed link (PFB produces no output without an input).
    app.update_dsp();
    REQUIRE(pfb.node().inputs[0] == nullptr);
}
