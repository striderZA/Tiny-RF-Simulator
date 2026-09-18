// Project-file round trip for the SPDT switch (rf_switch).
//
// The switch is persisted by its registry `project_type` key, so this covers
// what the engine-level serialize/deserialize case in test_rf_switch.cpp
// cannot: the registry lookup, the serializer, and the restoration of a link
// attached to the switch's SECOND output pin (T2). Shares the engine-level
// assertions' intent but exercises the real save/load path.
//
// A standalone executable rather than a case in the main `tests` binary:
// MinGW-w64 silently drops TEST_CASEs registered past that binary's ceiling,
// and CI guards that binary's registration floor (see tests/CMakeLists.txt).
#include "app.h"
#include "attenuator_engine.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "rf_switch_engine.h"
#include "signal_generator_engine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include <string>

using Catch::Approx;

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

TEST_CASE_METHOD(ImGuiFixture, "Round-trip: SPDT switch params and its T2 link survive save/load",
                 "[project_file][rf_switch]") {
    // Absolute temp path: this standalone executable runs with the source tree
    // as its CTest working directory, so a relative name would write into it.
    const std::string path =
        (std::filesystem::temp_directory_path() / "test_rf_switch_project.rfsim").string();
    std::remove(path.c_str());
    {
        RfSimulatorApp app;
        app.newProject();

        auto &gen = app.testComponents().add<SignalGeneratorEngine>(10001, app.testGraphEngine());
        auto &sw = app.testComponents().add<RFSwitchEngine>(10002, app.testGraphEngine());
        auto &att = app.testComponents().add<AttenuatorEngine>(10003, app.testGraphEngine());

        sw.setActiveThrow(1); // T2 active
        sw.setInsertionLoss_dB(1.25);
        sw.setIsolation_dB(55.0);
        gen.addTone(100e6, -20.0);

        app.testGraphEngine().addLink(gen.outputPinId(), sw.inputPinId());
        app.testGraphEngine().addLink(sw.outputPinId(1), att.inputPinId());

        app.saveProject(path);
    }
    {
        RfSimulatorApp app;
        app.loadProject(path);

        auto switches = app.testComponents().byType<RFSwitchEngine>();
        REQUIRE(switches.size() == 1);
        auto *sw = switches[0];
        CHECK(sw->activeThrow() == 1);
        CHECK(sw->insertionLoss_dB() == Approx(1.25));
        CHECK(sw->isolation_dB() == Approx(55.0));
        CHECK(sw->numOutputPins() == 2);

        auto attens = app.testComponents().byType<AttenuatorEngine>();
        REQUIRE(attens.size() == 1);
        auto *att = attens[0];

        app.update_dsp();

        // T2 is output port 1: the restored link must bind that port, not 0.
        REQUIRE(sw->node().inputs[0] != nullptr);
        REQUIRE(att->node().inputs[0] == &sw->node().outputs[1]);

        // And the two ends carry the configured throw: T2 is active, so the
        // attenuator sees the insertion loss (1.25 dB), not the isolation
        // floor its other output sits at.
        REQUIRE(sw->node().outputs[1].tones.size() == 1);
        CHECK(att->node().outputs[0].tones.size() == 1);
        CHECK(sw->node().outputs[1].tones[0].power_dBm == Approx(-20.0 - 1.25));
        CHECK(att->node().outputs[0].tones[0].power_dBm == Approx(-20.0 - 1.25));
    }
    std::remove(path.c_str());
}
