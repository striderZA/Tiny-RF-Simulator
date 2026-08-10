#include "app.h"
#include "coax_cable_engine.h"
#include "combiner_engine.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <complex>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <numbers>
#include <string>

using Catch::Approx;

// Generate a unique temp filename per call so parallel test processes don't
// step on each other's files.
static int s_temp_counter = 0;
static std::string tempPath(const std::string &suffix = "") {
    return "test_roundtrip_" + std::to_string(s_temp_counter++) + suffix + ".rfsim";
}

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

// ---------------------------------------------------------------------------
// 1 — Save the default app (2 components), reload, verify count
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Round-trip: empty project", "[project_file]") {
    auto path = tempPath();
    std::remove(path.c_str());
    {
        RfSimulatorApp app;
        app.saveProject(path);
        REQUIRE(app.isDirty() == false);
    }
    {
        RfSimulatorApp app;
        app.loadProject(path);
        REQUIRE(app.isDirty() == false);
        REQUIRE(app.componentCount() == 2); // default gen + amp
    }
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// 2 — Create an actual link between the two default components, save, reload,
//     verify the link survived.
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Round-trip: single generator and amplifier with link",
                 "[project_file]") {
    auto path = tempPath();
    std::remove(path.c_str());
    {
        RfSimulatorApp app;

        // The app starts with a SignalGenerator (index 0) and an Amplifier (index 1).
        auto comps = app.testComponents().all();
        REQUIRE(comps.size() == 2);

        int start_pin = comps[0]->outputPinId(0);
        int end_pin = comps[1]->inputPinId(0);
        int link_id = app.testGraphEngine().addLink(start_pin, end_pin);
        REQUIRE(link_id > 0);
        REQUIRE(app.testGraphEngine().links().size() == 1);

        app.saveProject(path);
    }
    {
        RfSimulatorApp app;
        app.loadProject(path);
        REQUIRE(app.componentCount() == 2);
        REQUIRE(app.testGraphEngine().links().size() == 1);
    }
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// 3 — newProject clears everything; save + reload stays empty
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Round-trip: zero components after newProject", "[project_file]") {
    auto path = tempPath();
    std::remove(path.c_str());
    {
        RfSimulatorApp app;
        app.newProject();
        app.saveProject(path);
        REQUIRE(app.componentCount() == 0);
    }
    {
        RfSimulatorApp app;
        app.loadProject(path);
        REQUIRE(app.componentCount() == 0);
    }
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// 4 — Discard via newProject clears dirty flag
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Discard clears dirty state", "[project_file]") {
    {
        RfSimulatorApp app;
        REQUIRE(app.componentCount() == 2);
        REQUIRE(app.isDirty() == false);

        // Mark dirty — simulates unsaved changes
        app.testMakeDirty();
        REQUIRE(app.isDirty() == true);

        // Discard via newProject (same path as Discard button for New/Open)
        app.newProject();
        REQUIRE(app.isDirty() == false);
        REQUIRE(app.componentCount() == 0);
    }
}

// ---------------------------------------------------------------------------
// 5 — Save a dirty project, reload, verify content
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Discard then save round-trip", "[project_file]") {
    auto path = tempPath();
    std::remove(path.c_str());
    {
        RfSimulatorApp app;
        app.testMakeDirty();
        REQUIRE(app.isDirty() == true);

        // Save the dirty project
        app.saveProject(path);
        REQUIRE(app.isDirty() == false);
    }
    {
        // Open it fresh — should have default 2 components (save was from dirty default state)
        RfSimulatorApp app;
        app.loadProject(path);
        REQUIRE(app.isDirty() == false);
        REQUIRE(app.componentCount() == 2);
    }
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// 6 — Loading garbage JSON doesn't crash and leaves a clean default project
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Load invalid JSON does not crash", "[project_file]") {
    auto path = tempPath();
    std::remove(path.c_str());
    {
        std::ofstream out(path);
        out << "not valid json {{{";
    }
    {
        RfSimulatorApp app;
        app.loadProject(path);
        REQUIRE(app.componentCount() == 2);
    }
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// 6b — Loading valid JSON with the wrong shape must not crash: either it loads
//      as an empty project or it fails gracefully, leaving a usable app.
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Load empty object JSON is a valid empty project",
                 "[project_file]") {
    auto path = tempPath("_empty");
    std::remove(path.c_str());
    {
        std::ofstream out(path);
        out << "{}";
    }
    {
        RfSimulatorApp app;
        app.loadProject(path); // must not crash (regression: shape access was unguarded)
        REQUIRE(app.componentCount() == 0); // {} is a valid (empty) project
    }
    std::remove(path.c_str());
}

TEST_CASE_METHOD(ImGuiFixture, "Load wrong-shape JSON fails gracefully, no crash",
                 "[project_file]") {
    SECTION("components is not an array") {
        auto path = tempPath("_comp5");
        std::remove(path.c_str());
        {
            std::ofstream out(path);
            out << R"({"components": 5})";
        }
        {
            RfSimulatorApp app;
            app.loadProject(path); // must not crash; load fails, project cleared
            REQUIRE(app.componentCount() == 0);
        }
        std::remove(path.c_str());
    }
    SECTION("component entry wrong-typed (type is a number)") {
        auto path = tempPath("_typed");
        std::remove(path.c_str());
        {
            std::ofstream out(path);
            out << R"({"components": [{"type": 42}]})";
        }
        {
            RfSimulatorApp app;
            app.loadProject(path);              // must not crash; bad component is skipped
            REQUIRE(app.componentCount() == 0); // skipped, nothing else to load
        }
        std::remove(path.c_str());
    }
    SECTION("window_state wrong-typed") {
        auto path = tempPath("_ws");
        std::remove(path.c_str());
        {
            std::ofstream out(path);
            out << R"({"window_state": 5})";
        }
        {
            RfSimulatorApp app;
            app.loadProject(path); // must not crash; load fails, project cleared
            REQUIRE(app.componentCount() == 0);
        }
        std::remove(path.c_str());
    }
}

// ---------------------------------------------------------------------------
// 7 — Add components with custom parameter values, save, reload, verify that
//     every parameter survived the round-trip.
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Round-trip: parameter values survive save/load", "[project_file]") {
    auto path = tempPath();
    std::remove(path.c_str());
    {
        RfSimulatorApp app;
        app.newProject();

        auto &gen = app.testComponents().add<SignalGeneratorEngine>(10001, app.testGraphEngine());
        gen.addTone(200e6, -10.0);
        gen.setFs_Hz(500e6);

        auto &amp = app.testComponents().add<AmplifierEngine>(10002, app.testGraphEngine());
        amp.setGain_dB(20.0);
        amp.setNF_dB(3.5);

        auto &mixer = app.testComponents().add<MixerEngine>(10003, app.testGraphEngine());
        mixer.setLoFreq_Hz(2.4e9);
        mixer.setConversionGain_dB(8.0);
        mixer.setNF_dB(5.0);

        auto &coax = app.testComponents().add<CoaxCableEngine>(10004, app.testGraphEngine());
        coax.setLengthM(2.5);

        REQUIRE(app.componentCount() == 4);
        app.saveProject(path);
        REQUIRE(app.isDirty() == false);
    }
    {
        RfSimulatorApp app;
        app.loadProject(path);
        REQUIRE(app.componentCount() == 4);

        // SignalGenerator
        auto gens = app.testComponents().byType<SignalGeneratorEngine>();
        REQUIRE(gens.size() == 1);
        CHECK(gens[0]->toneCount() == 1);
        CHECK(gens[0]->tones()[0].freq_Hz == Approx(200e6));
        CHECK(gens[0]->tones()[0].power_dBm == Approx(-10.0));

        // Amplifier
        auto amps = app.testComponents().byType<AmplifierEngine>();
        REQUIRE(amps.size() == 1);
        CHECK(amps[0]->gain_dB() == Approx(20.0));
        CHECK(amps[0]->nf_dB() == Approx(3.5));

        // Mixer
        auto mixers = app.testComponents().byType<MixerEngine>();
        REQUIRE(mixers.size() == 1);
        CHECK(mixers[0]->loFreq_Hz() == Approx(2.4e9));
        CHECK(mixers[0]->conversionGain_dB() == Approx(8.0));
        CHECK(mixers[0]->nf_dB() == Approx(5.0));

        // Coax cable
        auto coaxs = app.testComponents().byType<CoaxCableEngine>();
        REQUIRE(coaxs.size() == 1);
        CHECK(coaxs[0]->lengthM() == Approx(2.5));
    }
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// 8 — Create a group containing components, save, reload, verify the group
//     (name, member count) survived.
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Round-trip: groups survive save/load", "[project_file]") {
    auto path = tempPath();
    std::remove(path.c_str());
    {
        RfSimulatorApp app;
        app.newProject();

        app.testComponents().add<SignalGeneratorEngine>(10001, app.testGraphEngine());
        app.testComponents().add<AmplifierEngine>(10002, app.testGraphEngine());
        app.testComponents().add<MixerEngine>(10003, app.testGraphEngine());
        REQUIRE(app.componentCount() == 3);

        // Group the first two components
        auto comps = app.testComponents().all();
        REQUIRE(comps.size() == 3);
        std::vector<int> member_ids = {comps[0]->graphNodeId(), comps[1]->graphNodeId()};
        int gid = app.testGraphEngine().addGroup("RF Frontend", member_ids);
        REQUIRE(gid >= 0);
        REQUIRE(app.testGraphEngine().numGroups() == 1);

        app.saveProject(path);
    }
    {
        RfSimulatorApp app;
        app.loadProject(path);
        REQUIRE(app.componentCount() == 3);
        REQUIRE(app.testGraphEngine().numGroups() == 1);

        const auto &groups = app.testGraphEngine().groups();
        REQUIRE(groups.size() == 1);
        CHECK(groups[0].name == "RF Frontend");
        CHECK(groups[0].member_node_ids.size() == 2);
    }
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// 9 — Round-trip Attenuator, Combiner, and Equalizer with modified parameters
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Round-trip: Attenuator, Combiner, Equalizer", "[project_file]") {
    auto path = tempPath();
    std::remove(path.c_str());
    {
        RfSimulatorApp app;
        app.newProject();

        auto &atten = app.testComponents().add<AttenuatorEngine>(10001, app.testGraphEngine());
        atten.setAttenuation(15.5);

        auto &comb = app.testComponents().add<CombinerEngine>(10002, app.testGraphEngine());
        comb.setManualMode(true);

        auto &eq = app.testComponents().add<EqualizerEngine>(10003, app.testGraphEngine());
        eq.setRefGain_dB(-3.0);
        eq.setRefFreq_Hz(500e6);
        eq.setSlope_dBPerDecade(6.0);

        REQUIRE(app.componentCount() == 3);
        app.saveProject(path);
    }
    {
        RfSimulatorApp app;
        app.loadProject(path);
        REQUIRE(app.componentCount() == 3);

        auto attens = app.testComponents().byType<AttenuatorEngine>();
        REQUIRE(attens.size() == 1);
        CHECK(attens[0]->attenuation() == Approx(15.5));

        auto combs = app.testComponents().byType<CombinerEngine>();
        REQUIRE(combs.size() == 1);
        CHECK(combs[0]->manualMode() == true);

        auto eqs = app.testComponents().byType<EqualizerEngine>();
        REQUIRE(eqs.size() == 1);
        CHECK(eqs[0]->refGain_dB() == Approx(-3.0));
        CHECK(eqs[0]->refFreq_Hz() == Approx(500e6));
        CHECK(eqs[0]->slope_dBPerDecade() == Approx(6.0));
    }
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// 10 — Save/load preserves amplifier P1dB
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Project save/load preserves amplifier P1dB", "[project][p1db]") {
    auto path = tempPath();
    std::filesystem::remove(path);
    {
        RfSimulatorApp app;
        app.newProject();

        auto &amp = app.testComponents().add<AmplifierEngine>(100, app.testGraphEngine());
        amp.setGain_dB(20.0);
        amp.setNF_dB(2.5);
        amp.setP1dB_dBm(15.0);
        amp.setEnableNonlinear(true);

        app.saveProject(path);
    }
    {
        RfSimulatorApp app;
        app.loadProject(path);

        auto amps = app.testComponents().byType<AmplifierEngine>();
        REQUIRE(amps.size() == 1);
        REQUIRE(amps[0]->p1db_dBm() == Catch::Approx(15.0));
        REQUIRE(amps[0]->gain_dB() == Catch::Approx(20.0));
        REQUIRE(amps[0]->nf_dB() == Catch::Approx(2.5));
    }
    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 11 — Component positions survive save/load round-trip
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Round-trip: component positions survive save/load",
                 "[project_file]") {
    auto path = tempPath();
    std::remove(path.c_str());
    {
        RfSimulatorApp app;
        app.newProject();

        auto &gen = app.testComponents().add<SignalGeneratorEngine>(10001, app.testGraphEngine());
        auto &amp = app.testComponents().add<AmplifierEngine>(10002, app.testGraphEngine());
        REQUIRE(app.componentCount() == 2);

        // Register nodes in the pool first, then set explicit positions
        app.testGraphWidget().syncNodesFromEngine();
        ImNodes::EditorContextSet(app.testGraphWidget().context());
        ImNodes::SetNodeEditorSpacePos(gen.graphNodeId(), ImVec2(150.0f, 250.0f));
        ImNodes::SetNodeEditorSpacePos(amp.graphNodeId(), ImVec2(350.0f, 100.0f));

        app.saveProject(path);
    }
    {
        RfSimulatorApp app;
        app.loadProject(path);

        REQUIRE(app.componentCount() == 2);

        auto gens = app.testComponents().byType<SignalGeneratorEngine>();
        REQUIRE(gens.size() == 1);
        auto amps = app.testComponents().byType<AmplifierEngine>();
        REQUIRE(amps.size() == 1);

        // Verify positions survived round-trip
        ImNodes::EditorContextSet(app.testGraphWidget().context());
        ImVec2 gen_pos = ImNodes::GetNodeEditorSpacePos(gens[0]->graphNodeId());
        CHECK(gen_pos.x == Catch::Approx(150.0f));
        CHECK(gen_pos.y == Catch::Approx(250.0f));

        ImVec2 amp_pos = ImNodes::GetNodeEditorSpacePos(amps[0]->graphNodeId());
        CHECK(amp_pos.x == Catch::Approx(350.0f));
        CHECK(amp_pos.y == Catch::Approx(100.0f));
    }
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// 12 — Default (unrendered) component positions are (0,0)
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Round-trip: default component positions are (0,0)",
                 "[project_file]") {
    auto path = tempPath();
    std::remove(path.c_str());
    {
        RfSimulatorApp app;
        app.newProject();

        auto &gen = app.testComponents().add<SignalGeneratorEngine>(10001, app.testGraphEngine());
        auto &amp = app.testComponents().add<AmplifierEngine>(10002, app.testGraphEngine());

        // syncNodesFromEngine called by constructor registers default positions.
        // Save without moving nodes — should preserve (0,0).
        app.saveProject(path);
    }
    {
        RfSimulatorApp app;
        app.loadProject(path);

        REQUIRE(app.componentCount() == 2);

        auto gens = app.testComponents().byType<SignalGeneratorEngine>();
        REQUIRE(gens.size() == 1);
        auto amps = app.testComponents().byType<AmplifierEngine>();
        REQUIRE(amps.size() == 1);

        // Default unrendered nodes should still be at (0,0)
        ImNodes::EditorContextSet(app.testGraphWidget().context());
        ImVec2 gen_pos = ImNodes::GetNodeEditorSpacePos(gens[0]->graphNodeId());
        CHECK(gen_pos.x == Catch::Approx(0.0f));
        CHECK(gen_pos.y == Catch::Approx(0.0f));

        ImVec2 amp_pos = ImNodes::GetNodeEditorSpacePos(amps[0]->graphNodeId());
        CHECK(amp_pos.x == Catch::Approx(0.0f));
        CHECK(amp_pos.y == Catch::Approx(0.0f));
    }
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// 13 — Issue #56: S-param mode survives save/load for amplifier, ideal filter,
//      equalizer, attenuator, and combiner. Previously deserialize() restored
//      sparam_mode/sparam_filepath but never reloaded the Touchstone file, so
//      a reloaded project silently fell back to ideal/manual mode.
// ---------------------------------------------------------------------------
static std::string sparamFixturePath() {
    return std::string(PROJECT_SOURCE_DIR) +
           "/component_data/amplifiers/adm-3844psm/ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
}

TEST_CASE_METHOD(ImGuiFixture, "Round-trip: S-param mode survives save/load (issue #56)",
                 "[project_file][sparam]") {
    auto path = tempPath();
    std::remove(path.c_str());
    const std::string s2p = sparamFixturePath();
    // S1 containment (2026-08-09): S-param paths in project files resolve
    // against the project file's directory and must stay inside it, so the
    // fixture is staged next to the project file and referenced by its
    // relative name (the project file itself lives in the CWD).
    const std::string local_s2p = tempPath("_fixture.s2p");
    std::filesystem::copy_file(s2p, local_s2p, std::filesystem::copy_options::overwrite_existing);
    {
        RfSimulatorApp app;
        app.newProject();

        auto &amp = app.testComponents().add<AmplifierEngine>(10001, app.testGraphEngine());
        amp.setSParamFilepath(local_s2p);
        REQUIRE(amp.sparamLoaded());

        auto &flt = app.testComponents().add<IdealFilterEngine>(10002, app.testGraphEngine());
        flt.setSParamFilepath(local_s2p);
        REQUIRE(flt.sparamLoaded());

        auto &eq = app.testComponents().add<EqualizerEngine>(10003, app.testGraphEngine());
        eq.setSParamFilepath(local_s2p);
        REQUIRE(eq.sparamLoaded());

        auto &atten = app.testComponents().add<AttenuatorEngine>(10004, app.testGraphEngine());
        atten.setSParamFilepath(s2p);
        REQUIRE(atten.sparamMode());

        auto &comb = app.testComponents().add<CombinerEngine>(10005, app.testGraphEngine());
        comb.setSParamFilepath(s2p);
        REQUIRE(comb.sparamMode());

        REQUIRE(app.componentCount() == 5);
        app.saveProject(path);
    }
    {
        RfSimulatorApp app;
        app.loadProject(path);
        REQUIRE(app.componentCount() == 5);

        auto amps = app.testComponents().byType<AmplifierEngine>();
        REQUIRE(amps.size() == 1);
        CHECK(amps[0]->sparamMode() == true);
        CHECK(amps[0]->sparamLoaded() == true);

        // Issue #56 regression: sparamMode()/sparamLoaded() are necessary but
        // not sufficient — the old deserialize() restored the mode flags and
        // filepath without reloading the Touchstone file, so a reloaded
        // amplifier reported sparamMode()==true but applied ideal gain. Drive
        // a tone through the loaded amplifier via the app DSP chain and
        // compare the output against the S21-derived expectation (mirrors
        // tests/test_amplifier_sparam.cpp).
        auto &gen = app.testComponents().add<SignalGeneratorEngine>(20001, app.testGraphEngine());
        gen.addTone(1e9, -20.0);
        gen.update(0.0);

        int gen_pin = gen.outputPinId();
        int amp_pin = amps[0]->inputPinId();
        int link_id = app.testGraphEngine().addLink(gen_pin, amp_pin);
        REQUIRE(link_id > 0);

        app.update_dsp();

        const auto &amp_out = amps[0]->node().outputs[0];
        REQUIRE(amp_out.tones.size() == 1);
        auto S21 = amps[0]->sparamData().interpolate(1e9, 2);
        double expected_gain = 20.0 * std::log10(std::abs(S21));
        REQUIRE(amp_out.tones[0].power_dBm == Approx(-20.0 + expected_gain).margin(0.5));
        double expected_phase = std::arg(S21) * 180.0 / std::numbers::pi;
        REQUIRE(amp_out.tones[0].phase_deg == Approx(expected_phase).margin(1.0));

        auto flts = app.testComponents().byType<IdealFilterEngine>();
        REQUIRE(flts.size() == 1);
        CHECK(flts[0]->sparamMode() == true);
        CHECK(flts[0]->sparamLoaded() == true);

        auto eqs = app.testComponents().byType<EqualizerEngine>();
        REQUIRE(eqs.size() == 1);
        CHECK(eqs[0]->sparamMode() == true);
        CHECK(eqs[0]->sparamLoaded() == true);

        auto attens = app.testComponents().byType<AttenuatorEngine>();
        REQUIRE(attens.size() == 1);
        CHECK(attens[0]->sparamMode() == true);

        auto combs = app.testComponents().byType<CombinerEngine>();
        REQUIRE(combs.size() == 1);
        CHECK(combs[0]->sparamMode() == true);
    }
    std::remove(path.c_str());
    std::filesystem::remove(local_s2p);
}