#include "app.h"
#include "session_state.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"

static std::string tempPath() {
    return "test_project_rtt.rfsim";
}

static void cleanup() {
    std::remove(tempPath().c_str());
}

struct ImNodesFixture {
    ImNodesFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
        SessionState().setLastProject("");
    }
    ~ImNodesFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

// Register all graph engine nodes with the ImNodes editor context so that
// GetNodeEditorSpacePos works without prior rendering.
static void registerGraphNodes(RfSimulatorApp& app) {
    ImNodes::EditorContextSet(app.m_graph_widget->context());
    for (const auto& gn : app.m_graph_engine.nodes()) {
        ImNodes::SetNodeEditorSpacePos(gn.node_id, ImVec2(0, 0));
    }
}

TEST_CASE_METHOD(ImNodesFixture, "Round-trip: empty project", "[project_file]") {
    cleanup();
    {
        RfSimulatorApp app;
        registerGraphNodes(app);
        app.saveProject(tempPath());
        REQUIRE(app.isDirty() == false);
    }
    {
        RfSimulatorApp app;
        app.loadProject(tempPath());
        REQUIRE(app.isDirty() == false);
    }
    cleanup();
}

TEST_CASE_METHOD(ImNodesFixture, "Round-trip: generator + amplifier + link", "[project_file]") {
    cleanup();
    {
        RfSimulatorApp app;
        app.newProject();
        app.addGenerator();
        app.m_generators.back()->addTone(150e6, -10.0, 45.0);
        app.addAmplifier();
        app.m_amplifiers.back()->setGain_dB(15.0);
        app.m_amplifiers.back()->setNF_dB(2.5);
        app.m_amplifiers.back()->setEnableNonlinear(true);
        app.m_amplifiers.back()->setOIP3_dBm(35.0);

        int gen_pin = app.m_generators.back()->outputPinId();
        int amp_pin = app.m_amplifiers.back()->inputPinId();
        app.m_graph_engine.addLink(gen_pin, amp_pin);

        registerGraphNodes(app);
        app.saveProject(tempPath());
        REQUIRE(app.isDirty() == false);
    }
    {
        RfSimulatorApp app;
        app.loadProject(tempPath());

        REQUIRE(app.m_generators.size() == 1);
        REQUIRE(app.m_amplifiers.size() == 1);
        REQUIRE(app.m_generators[0]->toneCount() == 1);
        REQUIRE(app.m_generators[0]->tones()[0].freq_Hz == 150e6);
        REQUIRE(app.m_generators[0]->tones()[0].power_dBm == -10.0);
        REQUIRE(app.m_generators[0]->tones()[0].phase_deg == 45.0);

        REQUIRE(app.m_amplifiers[0]->gain_dB() == 15.0);
        REQUIRE(app.m_amplifiers[0]->nf_dB() == 2.5);
        REQUIRE(app.m_amplifiers[0]->enableNonlinear() == true);
        REQUIRE(app.m_amplifiers[0]->oip3_dBm() == 35.0);

        REQUIRE(app.m_graph_engine.links().size() == 1);
        REQUIRE(app.isDirty() == false);
    }
    cleanup();
}

TEST_CASE_METHOD(ImNodesFixture, "Round-trip: all component types", "[project_file]") {
    cleanup();
    {
        RfSimulatorApp app;
        app.newProject();
        app.addGenerator();
        app.m_generators.back()->addTone(100e6, 0.0);
        app.addAmplifier();
        app.m_amplifiers.back()->setGain_dB(10.0);
        app.addSplitter();
        app.addMixer();
        app.m_mixers.back()->setLoFreq_Hz(2e9);
        app.addSParamAmp();
        app.addSParamFilter();
        app.addAdc();
        app.m_adcs.back()->setBits(14);
        app.addPFB();
        app.m_pfbs.back()->setChannelCount(64);

        registerGraphNodes(app);
        app.saveProject(tempPath());
    }
    {
        RfSimulatorApp app;
        app.loadProject(tempPath());

        REQUIRE(app.m_generators.size() == 1);
        REQUIRE(app.m_amplifiers.size() == 1);
        REQUIRE(app.m_splitters.size() == 1);
        REQUIRE(app.m_mixers.size() == 1);
        REQUIRE(app.m_sparam_amps.size() == 1);
        REQUIRE(app.m_sparam_filters.size() == 1);
        REQUIRE(app.m_adcs.size() == 1);
        REQUIRE(app.m_pfbs.size() == 1);

        REQUIRE(app.m_mixers[0]->loFreq_Hz() == 2e9);
        REQUIRE(app.m_adcs[0]->bits() == 14);
        REQUIRE(app.m_pfbs[0]->channelCount() == 64);
    }
    cleanup();
}

TEST_CASE_METHOD(ImNodesFixture, "Load invalid JSON file does not crash", "[project_file]") {
    cleanup();
    {
        std::ofstream out(tempPath());
        out << "not valid json {{{";
        out.close();
    }
    {
        RfSimulatorApp app;
        app.loadProject(tempPath());
        // loadProject catches json exception and returns without clearing state
        REQUIRE(app.m_generators.size() == 1);
    }
    cleanup();
}

TEST_CASE_METHOD(ImNodesFixture, "Missing params use defaults", "[project_file]") {
    cleanup();
    {
        nlohmann::json root;
        root["version"] = 1;
        root["graph"]["nodes"] = nlohmann::json::array();
        nlohmann::json node;
        node["node_id"] = 1;
        node["type"] = "Amplifier";
        node["label"] = "Amp 0";
        node["x"] = 0.0;
        node["y"] = 0.0;
        root["graph"]["nodes"].push_back(node);
        root["graph"]["links"] = nlohmann::json::array();
        root["graph"]["probe_pins"] = nlohmann::json::array();
        std::ofstream out(tempPath());
        out << root.dump(2);
        out.close();
    }
    {
        RfSimulatorApp app;
        app.loadProject(tempPath());
        REQUIRE(app.m_amplifiers.size() == 1);
        REQUIRE(app.m_amplifiers[0]->gain_dB() == 0.0);
        REQUIRE(app.m_amplifiers[0]->nf_dB() == 0.0);
    }
    cleanup();
}
