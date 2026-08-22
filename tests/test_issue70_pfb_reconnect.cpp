// Regression test for GitHub issue #70:
// "PFB Channelizer input inconsistency"
//
// The PFB must not retain an ADC-derived sample rate after the ADC is removed
// and the original generator is connected directly again.
#include "adc_engine.h"
#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "pfb_channelizer_engine.h"
#include "signal_generator_engine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

struct Issue70ImGuiFixture {
    Issue70ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
    }
    ~Issue70ImGuiFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

TEST_CASE_METHOD(Issue70ImGuiFixture,
                 "PFB resets inherited Fs after ADC removal and direct reconnect (issue #70)",
                 "[app][pfb][regression][issue70]") {
    RfSimulatorApp app;
    app.newProject();

    auto &gen = app.testComponents().add<SignalGeneratorEngine>(10001, app.testGraphEngine());
    gen.addTone(100e6, -20.0);
    auto &adc = app.testComponents().add<AdcEngine>(10002, app.testGraphEngine());
    auto &pfb = app.testComponents().add<PFBChannelizerEngine>(10003, app.testGraphEngine());

    int direct_link = app.testGraphEngine().addLink(gen.outputPinId(), pfb.inputPinId());
    app.update_dsp();
    REQUIRE(pfb.fs_Hz() == 0.0);
    REQUIRE(pfb.node().outputs[0].frequencies.empty());

    app.testGraphEngine().removeLink(direct_link);
    app.testGraphEngine().addLink(gen.outputPinId(), adc.inputPinId());
    app.testGraphEngine().addLink(adc.outputPinId(), pfb.inputPinId());
    app.update_dsp();
    REQUIRE(pfb.fs_Hz() == Catch::Approx(500e6));
    REQUIRE_FALSE(pfb.node().outputs[0].frequencies.empty());

    REQUIRE(app.testGraphWidget().onRemoveNode);
    app.testGraphWidget().onRemoveNode(adc.graphNodeId());
    app.update_dsp();
    REQUIRE(pfb.node().inputs[0] == nullptr);

    app.testGraphEngine().addLink(gen.outputPinId(), pfb.inputPinId());
    app.update_dsp();

    REQUIRE(pfb.fs_Hz() == 0.0);
    REQUIRE(pfb.node().outputs[0].frequencies.empty());
    REQUIRE(pfb.node().outputs[1].frequencies.empty());
}
