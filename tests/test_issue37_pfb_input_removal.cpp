// Regression test for GitHub issue #37:
// "Segmentation Fault when removing PFB Channelizer input"
//
// Root cause: removing a component destroys its engine and owned SignalNode/Spectrum
// outputs synchronously mid-frame. Downstream inputs were only rewired at the start
// of update_dsp(), before the current frame's panels finished drawing, so a widget
// dereferencing node().inputs[] could observe a dangling pointer.
//
// Fix: CircuitRuntime::removeComponent() rewires surviving inputs immediately after
// registry removal. The app's shared removal action then rebuilds PFB views and group
// boundaries before drawing continues, preventing later panels from using stale
// Spectrum pointers.
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

    auto &gen = static_cast<SignalGeneratorEngine &>(*app.testCreateComponent("generator", 10001));
    auto &adc = static_cast<AdcEngine &>(*app.testCreateComponent("adc", 10002));
    auto &pfb = static_cast<PFBChannelizerEngine &>(*app.testCreateComponent("pfb", 10003));

    REQUIRE(app.testConnectLink(gen.outputPinId(), adc.inputPinId()).has_value());
    REQUIRE(app.testConnectLink(adc.outputPinId(), pfb.inputPinId()).has_value());

    // Normal frame: wires PFB's input to the ADC's output Spectrum.
    app.update_dsp();
    REQUIRE(pfb.node().inputs[0] == &adc.node().outputs[0]);

    int adc_graph_id = adc.graphNodeId();
    // Simulate deleting the ADC through the app's fixture command.
    REQUIRE(app.testRemoveComponent(adc_graph_id));

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
