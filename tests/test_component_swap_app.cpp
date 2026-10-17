// Issue #200: app-level side effects of an in-place library part swap. The UI-free command is
// covered by test_component_swap.cpp. These cases cover what RfSimulatorApp::swapLibraryPart()
// adds: the Network Analyzer Point A/B and the Power Meter source follow the successor's output,
// and a PFB keeps the IQ Plot and Channelizer Grid visibility the user set in this session.
#include "app.h"
#include "component_library.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include <catch2/catch_test_macros.hpp>
#include <string>

namespace {

struct SwapAppImGuiFixture {
    SwapAppImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
    }
    ~SwapAppImGuiFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

// Definitions are built in code, so these cases need no library files on disk.
ComponentDefinition makeDefinition(const std::string &type, const std::string &part_number,
                                   const nlohmann::json &parameters) {
    ComponentDefinition definition{};
    definition.type = type;
    definition.part_number = part_number;
    definition.parameters = parameters;
    return definition;
}

// The successor keeps the predecessor's component id under a new graph node, so it is found by id.
const IComponentEngine *findByComponentId(const RfSimulatorApp &app, int component_id) {
    for (const IComponentEngine *component : app.testComponents().all()) {
        if (component && component->id() == component_id)
            return component;
    }
    return nullptr;
}

} // namespace

TEST_CASE_METHOD(SwapAppImGuiFixture, "a swap keeps the IQ Plot and Grid a PFB's user hid",
                 "[app][issue200]") {
    RfSimulatorApp app;
    app.newProject();
    IComponentEngine *pfb = app.testCreateComponent("pfb", 10003);
    REQUIRE(pfb != nullptr);
    const int old_node = pfb->graphNodeId();
    REQUIRE(app.testPfbViewCount() == 1);
    app.testPfbViews().iqVisibility()[0] = false;
    app.testPfbViews().gridVisibility()[0] = false;

    const std::string refusal = app.testSwapLibraryPart(
        old_node, makeDefinition("pfb", "PFB-SWAP-B", nlohmann::json::object()));

    INFO("refusal: " << refusal);
    REQUIRE(refusal.empty());
    const IComponentEngine *successor = findByComponentId(app, 10003);
    REQUIRE(successor != nullptr);
    REQUIRE(successor->graphNodeId() != old_node);
    REQUIRE(app.testPfbViewCount() == 1);
    CHECK_FALSE(app.testPfbViews().iqVisibility()[0]);
    CHECK_FALSE(app.testPfbViews().gridVisibility()[0]);
}

TEST_CASE_METHOD(SwapAppImGuiFixture,
                 "a swap moves the Network Analyzer points and Power Meter source to the successor",
                 "[app][issue200]") {
    RfSimulatorApp app;
    app.newProject();
    IComponentEngine *generator = app.testCreateComponent("generator", 10001);
    IComponentEngine *amp = app.testCreateComponent("amplifier", 10002);
    REQUIRE(generator != nullptr);
    REQUIRE(amp != nullptr);
    REQUIRE(app.testConnectLink(generator->outputPinId(), amp->inputPinId()).has_value());
    const int generator_out = generator->outputPinId();
    const int old_out = amp->outputPinId();
    const int old_node = amp->graphNodeId();
    auto &na = app.testNetworkAnalyzerEngine();
    na.setPointA(generator_out);
    na.setPointB(old_out);
    app.testPowerMeterWidget().setSourcePin(old_out);

    const std::string refusal = app.testSwapLibraryPart(
        old_node, makeDefinition("amplifier", "AMP-SWAP-B",
                                 nlohmann::json{{"gain_dB", 20.0}, {"nf_dB", 2.0}}));

    INFO("refusal: " << refusal);
    REQUIRE(refusal.empty());
    const IComponentEngine *successor = findByComponentId(app, 10002);
    REQUIRE(successor != nullptr);
    const int new_out = successor->outputPinId();
    REQUIRE(new_out != old_out);
    // Point A stays on the upstream generator, which the swap does not touch; Point B and the
    // Power Meter source move to the successor's output, the only port that replaced the old one.
    CHECK(na.pointAPin() == generator_out);
    CHECK(na.pointBPin() == new_out);
    CHECK(app.testPowerMeterWidget().sourcePin() == new_out);
}
