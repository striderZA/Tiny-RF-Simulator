// Standalone regression coverage for the app-owned Test Flow runner panel.
//
// The flow runner drives the *live* circuit: RunFlow() deserializes each swept
// value into the real engines. The widget's contract is that a run is invisible
// to the project afterwards — every component's serialize() output, every graph
// link, and the project dirty flag must be exactly what they were before the
// run — and that a restoration failure is a latched, recoverable error instead
// of a silently corrupted circuit.
//
// This lives in its own executable (not `test_engine/ui_tests.cpp`) because the
// ImGui Test Engine registers all of its cases unconditionally with no argv
// filter, so a failure there cannot be run in isolation; `add_standalone_test`
// produces one CTest entry that runs on every CI leg. See tests/AGENTS.md.
#include "amplifier_engine.h"
#include "app.h"
#include "component_engine_base.h"
#include "component_registry.h"
#include "flow_runner.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "node_graph_engine.h"
#include "signal_generator_engine.h"
#include "test_flow_widget.h"
#include "test_temp_paths.h"
#include "view_manager.h"

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

namespace {

namespace fs = std::filesystem;

// One component's observable state. `component_id` is the flow-facing
// IComponentEngine::id(); `node_id` is the graph node id, which a flow must
// never be written against.
struct ComponentState {
    int node_id = -1;
    int component_id = -1;
    nlohmann::json state;
};

std::vector<ComponentState> snapshotComponents(ComponentRegistry &components) {
    std::vector<ComponentState> out;
    for (IComponentEngine *component : components.all()) {
        if (!component)
            continue;
        out.push_back({component->graphNodeId(), component->id(), component->serialize()});
    }
    return out;
}

std::vector<std::array<int, 3>> snapshotLinks(NodeGraphEngine &graph) {
    std::vector<std::array<int, 3>> out;
    for (const GraphLink &link : graph.links())
        out.push_back({link.link_id, link.start_pin_id, link.end_pin_id});
    return out;
}

fs::path uniqueTempPath(const std::string &tag) {
    return fs::temp_directory_path() /
           ("rfsim_test_flow_widget_" + tag + "_" + test_temp_paths::processTag() + ".json");
}

void writeText(const fs::path &path, const std::string &text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out.is_open());
    out << text;
}

struct ScopedRemove {
    fs::path path;
    ~ScopedRemove() {
        std::error_code error;
        fs::remove(path, error);
    }
};

// A `tones[0].power_dBm` sweep on the generator measured at the amplifier's
// output — the canonical flow shape, with `component` values that are engine
// ids (never graph node ids).
std::string sweepFlowJson(int generator_id, int amplifier_id, int sweeps = 3) {
    nlohmann::json values = nlohmann::json::array();
    for (int i = 0; i < sweeps; ++i)
        values.push_back(-30.0 + 10.0 * static_cast<double>(i));
    const nlohmann::json flow = {
        {"version", 1},
        {"name", "widget sweep"},
        {"conditions",
         nlohmann::json::array({nlohmann::json{
             {"component", generator_id}, {"path", "tones[0].power_dBm"}, {"values", values}}})},
        {"measure", nlohmann::json::array({nlohmann::json{
                        {"component", amplifier_id}, {"port", 0}, {"metric", "power_dBm"}}})}};
    return flow.dump(2);
}

// A sweep of one component's `value` key, measured on a second component.
std::string singleValueFlowJson(int swept_component, int measured_component, double value) {
    const nlohmann::json flow = {
        {"version", 1},
        {"name", "restore probe"},
        {"conditions",
         nlohmann::json::array({nlohmann::json{
             {"component", swept_component}, {"path", "value"}, {"values", {value}}}})},
        {"measure",
         nlohmann::json::array({nlohmann::json{
             {"component", measured_component}, {"port", 0}, {"metric", "power_dBm"}}})}};
    return flow.dump(2);
}

// A component that accepts a swept value but throws when its own baseline is
// written back — i.e. execution succeeds and only restoration fails, which is
// the case the widget must survive without corrupting the circuit silently.
class BaselineRejectingEngine final : public ComponentEngineBase {
  public:
    BaselineRejectingEngine(int id, NodeGraphEngine &graph, double baseline)
        : ComponentEngineBase(id, graph, "BaselineRejector", 0, 1), m_baseline(baseline),
          m_value(baseline) {}

    std::string_view type_name() const override { return "baseline_rejector"; }
    std::string hoverSummary() const override { return "BaselineRejector"; }
    void update(double) override {}

    nlohmann::json serialize() const override { return {{"value", m_value}}; }
    void deserialize(const nlohmann::json &snapshot) override {
        ++m_deserialize_calls;
        const double value = snapshot.at("value").get<double>();
        if (value == m_baseline)
            throw std::runtime_error("intentional baseline restore failure");
        m_value = value;
    }

    double value() const { return m_value; }
    int deserializeCalls() const { return m_deserialize_calls; }

  private:
    double m_baseline;
    double m_value;
    int m_deserialize_calls = 0;
};

// A well-behaved neighbour, declared *after* the rejecting engine so its
// successful restoration proves the restore loop continued past the failure.
class TrackingEngine final : public ComponentEngineBase {
  public:
    TrackingEngine(int id, NodeGraphEngine &graph, double initial)
        : ComponentEngineBase(id, graph, "Tracking", 0, 1), m_value(initial) {}

    std::string_view type_name() const override { return "tracking"; }
    std::string hoverSummary() const override { return "Tracking"; }
    void update(double) override {}

    nlohmann::json serialize() const override { return {{"value", m_value}}; }
    void deserialize(const nlohmann::json &snapshot) override {
        ++m_deserialize_calls;
        m_value = snapshot.at("value").get<double>();
    }

    double value() const { return m_value; }
    int deserializeCalls() const { return m_deserialize_calls; }

  private:
    double m_value;
    int m_deserialize_calls = 0;
};

} // namespace

// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture,
                 "TestFlowWidget: a sweep leaves every component, link, and dirty flag unchanged",
                 "[test_flow][widget]") {
    RfSimulatorApp app;
    SignalGeneratorEngine &generator =
        *app.testComponents().byType<SignalGeneratorEngine>().front();
    AmplifierEngine &amplifier = *app.testComponents().byType<AmplifierEngine>().front();
    app.testGraphEngine().addLink(generator.outputPinId(), amplifier.inputPinId());

    // Flow files address engines by IComponentEngine::id(); a graph node id
    // would resolve to nothing. Pin that here so the fixture cannot silently
    // start writing node ids.
    REQUIRE(generator.id() != generator.graphNodeId());
    REQUIRE(amplifier.id() != amplifier.graphNodeId());

    const fs::path path = uniqueTempPath("sweep");
    writeText(path, sweepFlowJson(generator.id(), amplifier.id()));
    ScopedRemove cleanup{path};

    const std::vector<ComponentState> components_before = snapshotComponents(app.testComponents());
    const std::vector<std::array<int, 3>> links_before = snapshotLinks(app.testGraphEngine());
    const bool dirty_before = app.isDirty();
    REQUIRE(components_before.size() == 2);

    TestFlowWidget &widget = app.testTestFlowWidget();
    REQUIRE(widget.loadFlow(path.string()));
    REQUIRE(widget.run());

    REQUIRE(widget.result().has_value());
    REQUIRE(widget.result()->ok);
    REQUIRE(widget.result()->rows.size() == 3);
    REQUIRE(widget.result()->rows[0].conditions[0].value == Catch::Approx(-30.0));
    REQUIRE(widget.result()->rows[2].conditions[0].value == Catch::Approx(-10.0));
    REQUIRE_FALSE(widget.restoreFailed());

    // The run swept the live generator and rewired the live amplifier; both
    // must be back exactly as they were.
    const std::vector<ComponentState> components_after = snapshotComponents(app.testComponents());
    REQUIRE(components_after.size() == components_before.size());
    for (size_t i = 0; i < components_before.size(); ++i) {
        REQUIRE(components_after[i].node_id == components_before[i].node_id);
        REQUIRE(components_after[i].component_id == components_before[i].component_id);
        REQUIRE(components_after[i].state == components_before[i].state);
    }
    REQUIRE(snapshotLinks(app.testGraphEngine()) == links_before);
    REQUIRE(app.isDirty() == dirty_before);
    REQUIRE_FALSE(app.isDirty());
}

// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture,
                 "TestFlowWidget: an unresolvable flow fails with zero rows and stays runnable",
                 "[test_flow][widget][failure]") {
    RfSimulatorApp app;
    SignalGeneratorEngine &generator =
        *app.testComponents().byType<SignalGeneratorEngine>().front();
    AmplifierEngine &amplifier = *app.testComponents().byType<AmplifierEngine>().front();
    app.testGraphEngine().addLink(generator.outputPinId(), amplifier.inputPinId());

    TestFlowWidget &widget = app.testTestFlowWidget();

    // A successful run first, so the failed one below can prove it replaced the
    // rows instead of leaving them on screen.
    const fs::path good_path = uniqueTempPath("good");
    writeText(good_path, sweepFlowJson(generator.id(), amplifier.id()));
    ScopedRemove good_cleanup{good_path};
    REQUIRE(widget.loadFlow(good_path.string()));
    REQUIRE(widget.run());
    REQUIRE(widget.result()->rows.size() == 3);

    const std::vector<ComponentState> components_before = snapshotComponents(app.testComponents());
    const std::vector<std::array<int, 3>> links_before = snapshotLinks(app.testGraphEngine());
    const bool dirty_before = app.isDirty();

    // Well-formed JSON whose condition targets an engine id that does not exist
    // in this circuit: the failure can only come from resolution.
    const fs::path missing_path = uniqueTempPath("missing");
    writeText(missing_path, sweepFlowJson(987654, amplifier.id()));
    ScopedRemove missing_cleanup{missing_path};

    REQUIRE(widget.loadFlow(missing_path.string()));
    // Selecting a flow clears the previous run's rows before parsing.
    REQUIRE_FALSE(widget.result().has_value());

    REQUIRE_FALSE(widget.run());
    REQUIRE(widget.result().has_value());
    REQUIRE_FALSE(widget.result()->ok);
    REQUIRE(widget.result()->error.code == FlowErrorCode::ComponentNotFound);
    REQUIRE(widget.result()->rows.empty());
    REQUIRE_FALSE(widget.restoreFailed());

    const std::vector<ComponentState> components_after = snapshotComponents(app.testComponents());
    for (size_t i = 0; i < components_before.size(); ++i)
        REQUIRE(components_after[i].state == components_before[i].state);
    REQUIRE(snapshotLinks(app.testGraphEngine()) == links_before);
    REQUIRE(app.isDirty() == dirty_before);

    // An ordinary failure must not poison the widget: the next valid run works.
    REQUIRE(widget.loadFlow(good_path.string()));
    REQUIRE(widget.run());
    REQUIRE(widget.result()->rows.size() == 3);
}

// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "TestFlowWidget: malformed flow JSON is rejected and cannot run",
                 "[test_flow][widget][failure]") {
    RfSimulatorApp app;
    TestFlowWidget &widget = app.testTestFlowWidget();

    const fs::path path = uniqueTempPath("malformed");
    writeText(path, "{\n  \"version\": 1,\n  \"measure\": [\n");
    ScopedRemove cleanup{path};

    REQUIRE_FALSE(widget.loadFlow(path.string()));
    REQUIRE_FALSE(widget.loadState().ok);
    REQUIRE(widget.loadState().error.code == FlowErrorCode::InvalidJson);
    REQUIRE_FALSE(widget.loadState().error.message.empty());

    REQUIRE_FALSE(widget.run());
    REQUIRE_FALSE(widget.result().has_value());
}

// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture,
                 "TestFlowWidget: a failed restoration latches and refuses later runs",
                 "[test_flow][widget][restore]") {
    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry components(graph, view);

    constexpr double kRejectedBaseline = 1.0;
    constexpr int kRejectingId = 700;
    constexpr int kTrackingId = 701;
    auto &rejecting =
        components.add<BaselineRejectingEngine>(kRejectingId, graph, kRejectedBaseline);
    auto &tracking = components.add<TrackingEngine>(kTrackingId, graph, 5.0);

    const fs::path path = uniqueTempPath("restore");
    // The sweep value is accepted, so execution itself succeeds and the only
    // failure is the restoration of the rejecting component's baseline.
    writeText(path, singleValueFlowJson(kRejectingId, kTrackingId, 2.0));
    ScopedRemove cleanup{path};

    TestFlowWidget widget(components, graph);
    REQUIRE(widget.loadFlow(path.string()));
    REQUIRE_FALSE(widget.run());

    REQUIRE(widget.restoreFailed());
    REQUIRE_FALSE(widget.result().has_value());
    REQUIRE(widget.statusMessage().find("estore") != std::string::npos);
    // The rejecting engine was swept, so the failure is a restoration failure
    // rather than a refused run.
    REQUIRE(rejecting.deserializeCalls() >= 1);
    // One component's failure did not strand the components after it.
    REQUIRE(tracking.deserializeCalls() >= 1);
    REQUIRE(tracking.value() == Catch::Approx(5.0));

    // Later runs are refused, not attempted.
    const int rejecting_calls = rejecting.deserializeCalls();
    const int tracking_calls = tracking.deserializeCalls();
    REQUIRE_FALSE(widget.run());
    REQUIRE(rejecting.deserializeCalls() == rejecting_calls);
    REQUIRE(tracking.deserializeCalls() == tracking_calls);

    // Only the documented circuit-reload recovery clears the latch.
    widget.resetAfterCircuitReload();
    REQUIRE_FALSE(widget.restoreFailed());
    REQUIRE_FALSE(widget.result().has_value());
}
