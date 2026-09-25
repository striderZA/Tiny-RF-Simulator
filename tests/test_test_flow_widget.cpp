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
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
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
        // draw() runs real headless frames in the panel tests: a bare context
        // has a (-1,-1) DisplaySize sentinel and an imgui.ini path, and needs an
        // explicitly built font atlas because there is no renderer backend
        // (NewFrame() asserts TexIsBuilt when RendererHasTextures is not set).
        ImGui::GetIO().DisplaySize = ImVec2(1920, 1080);
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char *atlas_pixels = nullptr;
        int atlas_w = 0;
        int atlas_h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&atlas_pixels, &atlas_w, &atlas_h);
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
    REQUIRE(components_after.size() == components_before.size());
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
    REQUIRE(widget.statusMessage().find("Restore failed") == 0);
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

// ===========================================================================
// Panel rendering / preview / export states
//
// These drive the model the panel renders (preview, row count, cell text) plus
// real headless ImGui frames for the button/callback contract. Everything here
// uses the seeded app circuit unless a case builds its own registry.
// ===========================================================================
namespace {

nlohmann::json measureJson(int component, int port, const std::string &metric) {
    return nlohmann::json{{"component", component}, {"port", port}, {"metric", metric}};
}

// A flow with no conditions at all: exactly one row, measured at `component`.
std::string noConditionFlowJson(int component, int port = 0) {
    const nlohmann::json flow = {
        {"version", 1},
        {"name", "no conditions"},
        {"measure", nlohmann::json::array({measureJson(component, port, "power_dBm")})}};
    return flow.dump(2);
}

// Two conditions with 3 and 4 values: the expected row count is their product.
std::string twoConditionFlowJson(int generator_id, int amplifier_id) {
    nlohmann::json conditions = nlohmann::json::array();
    conditions.push_back(nlohmann::json{{"component", generator_id},
                                        {"path", "tones[0].power_dBm"},
                                        {"values", {-30.0, -20.0, -10.0}}});
    conditions.push_back(nlohmann::json{
        {"component", amplifier_id}, {"path", "gain_dB"}, {"values", {0.0, 5.0, 10.0, 15.0}}});
    const nlohmann::json flow = {
        {"version", 1},
        {"name", "two conditions"},
        {"conditions", conditions},
        {"measure", nlohmann::json::array({measureJson(amplifier_id, 0, "power_dBm")})}};
    return flow.dump(2);
}

// A path whose parent directory does not exist, so ofstream cannot open it.
fs::path uniqueMissingDirectoryPath(const std::string &tag) {
    return fs::temp_directory_path() /
           ("rfsim_test_flow_widget_dir_" + tag + "_" + test_temp_paths::processTag()) /
           "results.json";
}

std::string readText(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.is_open());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// One headless frame of the panel. `open` is null so the window always renders.
// The window geometry is pinned so a rect read from one frame stays valid in the
// next: a fresh ImGui window auto-fits as its content grows, which would
// otherwise move the buttons between reading their rects and clicking them.
void drawFrame(TestFlowWidget &widget, const std::function<void()> &open_dialog,
               const std::function<void()> &export_dialog) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(760.0f, 620.0f), ImGuiCond_Always);
    widget.draw("Test Flow", nullptr, open_dialog, export_dialog);
    ImGui::Render();
}

// A real click on a button: press on one frame, release on the next, with the
// pointer parked at the centre of the rect draw() recorded. ImGui has no
// public label-based item lookup without the Test Engine, which this target
// deliberately does not link. The pointer is parked on the canvas corner and a
// frame is rendered first, so two clicks in a row are never merged into one
// double-click by ImGui's click-count heuristic.
void clickButton(TestFlowWidget &widget, const float rect[4],
                 const std::function<void()> &open_dialog,
                 const std::function<void()> &export_dialog) {
    const ImVec2 centre((rect[0] + rect[2]) * 0.5f, (rect[1] + rect[3]) * 0.5f);
    ImGui::GetIO().AddMousePosEvent(2.0f, 2.0f);
    drawFrame(widget, open_dialog, export_dialog);
    ImGui::GetIO().AddMousePosEvent(centre.x, centre.y);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    drawFrame(widget, open_dialog, export_dialog);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    drawFrame(widget, open_dialog, export_dialog);
}

// Lets the window settle (its auto-fit size is only known after one frame) and
// then returns the button rects the next frame will use.
TestFlowWidget::ButtonRects warmUpAndReadRects(TestFlowWidget &widget,
                                               const std::function<void()> &open_dialog,
                                               const std::function<void()> &export_dialog) {
    drawFrame(widget, open_dialog, export_dialog);
    drawFrame(widget, open_dialog, export_dialog);
    return widget.lastButtonRects();
}

} // namespace

// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture,
                 "TestFlowWidget: the preview resolves flow ids to live components and ports",
                 "[test_flow][widget][panel]") {
    RfSimulatorApp app;
    SignalGeneratorEngine &generator =
        *app.testComponents().byType<SignalGeneratorEngine>().front();
    AmplifierEngine &amplifier = *app.testComponents().byType<AmplifierEngine>().front();
    app.testGraphEngine().addLink(generator.outputPinId(), amplifier.inputPinId());

    const fs::path path = uniqueTempPath("preview");
    writeText(path, sweepFlowJson(generator.id(), amplifier.id(), 3));
    ScopedRemove cleanup{path};

    TestFlowWidget &widget = app.testTestFlowWidget();
    REQUIRE(widget.loadFlow(path.string()));

    const TestFlowWidget::FlowPreview preview = widget.preview();
    REQUIRE(preview.loaded);
    REQUIRE(preview.name == "widget sweep");
    REQUIRE(preview.runnable);
    REQUIRE(preview.issues.empty());
    REQUIRE(preview.expected_rows == 3);

    REQUIRE(preview.conditions.size() == 1);
    const TestFlowWidget::ConditionEntry &condition = preview.conditions[0];
    REQUIRE(condition.component == generator.id());
    REQUIRE(condition.path == "tones[0].power_dBm");
    REQUIRE(condition.resolved);
    REQUIRE_FALSE(condition.component_label.empty());
    REQUIRE(condition.value_count == 3);
    REQUIRE(condition.values.size() == 3);
    REQUIRE(condition.values[0] == Catch::Approx(-30.0));
    REQUIRE(condition.values[2] == Catch::Approx(-10.0));

    REQUIRE(preview.measurements.size() == 1);
    const TestFlowWidget::MeasurementEntry &measurement = preview.measurements[0];
    REQUIRE(measurement.component == amplifier.id());
    REQUIRE(measurement.port == 0);
    REQUIRE(measurement.component_resolved);
    REQUIRE(measurement.port_resolved);
    REQUIRE(measurement.output_ports == amplifier.node().outputs.size());
    REQUIRE(measurement.metric == "power_dBm");
    REQUIRE_FALSE(measurement.component_label.empty());

    // The table is one row per FlowRow, and the preview is what predicts it.
    REQUIRE(widget.run());
    REQUIRE(widget.result().has_value());
    REQUIRE(widget.result()->rows.size() == preview.expected_rows);
    REQUIRE(widget.result()->rows[0].conditions[0].component == generator.id());
    REQUIRE(widget.result()->rows[0].metrics[0].component == amplifier.id());

    // A flow with no conditions has exactly one row, not zero.
    const fs::path single_path = uniqueTempPath("no_condition");
    writeText(single_path, noConditionFlowJson(amplifier.id()));
    ScopedRemove single_cleanup{single_path};
    REQUIRE(widget.loadFlow(single_path.string()));
    REQUIRE(widget.preview().conditions.empty());
    REQUIRE(widget.preview().expected_rows == 1);
    REQUIRE(widget.preview().runnable);
    REQUIRE(widget.run());
    REQUIRE(widget.result()->rows.size() == 1);

    // Two conditions multiply: the table gets 3 x 4 = 12 rows, and the preview
    // predicts exactly that before the run.
    const fs::path two_path = uniqueTempPath("two_conditions");
    writeText(two_path, twoConditionFlowJson(generator.id(), amplifier.id()));
    ScopedRemove two_cleanup{two_path};
    REQUIRE(widget.loadFlow(two_path.string()));
    const TestFlowWidget::FlowPreview two = widget.preview();
    REQUIRE(two.conditions.size() == 2);
    REQUIRE(two.conditions[0].value_count == 3);
    REQUIRE(two.conditions[1].value_count == 4);
    REQUIRE(two.conditions[1].path == "gain_dB");
    REQUIRE(two.expected_rows == 12);
    REQUIRE(two.runnable);
    REQUIRE(widget.run());
    REQUIRE(widget.result()->rows.size() == 12);
    REQUIRE(widget.result()->rows.size() == two.expected_rows);
}

// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture,
                 "TestFlowWidget: the preview reports unresolvable ids and ports as not runnable",
                 "[test_flow][widget][panel][failure]") {
    RfSimulatorApp app;
    SignalGeneratorEngine &generator =
        *app.testComponents().byType<SignalGeneratorEngine>().front();
    AmplifierEngine &amplifier = *app.testComponents().byType<AmplifierEngine>().front();
    app.testGraphEngine().addLink(generator.outputPinId(), amplifier.inputPinId());

    TestFlowWidget &widget = app.testTestFlowWidget();

    // (1) A component id that exists nowhere in the circuit.
    const fs::path unknown_path = uniqueTempPath("preview_unknown");
    writeText(unknown_path, sweepFlowJson(987654, amplifier.id()));
    ScopedRemove unknown_cleanup{unknown_path};
    REQUIRE(widget.loadFlow(unknown_path.string()));
    const TestFlowWidget::FlowPreview unknown = widget.preview();
    REQUIRE(unknown.loaded);
    REQUIRE_FALSE(unknown.runnable);
    REQUIRE_FALSE(unknown.conditions[0].resolved);
    REQUIRE(unknown.conditions[0].component_label.empty());
    REQUIRE_FALSE(unknown.issues.empty());
    REQUIRE(unknown.issues[0].find("987654") != std::string::npos);

    // (2) The graph-node id is NOT the flow id. A flow written against a node id
    //     must stay unresolved, which is exactly what ComponentRegistry::find()
    //     (graph-node keyed) would get wrong.
    REQUIRE(generator.graphNodeId() != generator.id());
    const fs::path node_path = uniqueTempPath("preview_node_id");
    writeText(node_path, sweepFlowJson(generator.graphNodeId(), amplifier.id()));
    ScopedRemove node_cleanup{node_path};
    REQUIRE(widget.loadFlow(node_path.string()));
    REQUIRE_FALSE(widget.preview().runnable);
    REQUIRE_FALSE(widget.preview().conditions[0].resolved);

    // (3) An output port the component does not have.
    const fs::path port_path = uniqueTempPath("preview_port");
    writeText(port_path, sweepFlowJson(generator.id(), amplifier.id()));
    ScopedRemove port_cleanup{port_path};
    REQUIRE(widget.loadFlow(port_path.string()));
    REQUIRE(widget.preview().runnable);
    REQUIRE(widget.preview().measurements[0].port_resolved);

    const fs::path bad_port_path = uniqueTempPath("preview_bad_port");
    {
        const nlohmann::json flow = {
            {"version", 1},
            {"name", "bad port"},
            {"conditions", nlohmann::json::array({nlohmann::json{{"component", generator.id()},
                                                                 {"path", "tones[0].power_dBm"},
                                                                 {"values", {-30.0}}}})},
            {"measure", nlohmann::json::array({measureJson(amplifier.id(), 7, "power_dBm")})}};
        writeText(bad_port_path, flow.dump(2));
    }
    ScopedRemove bad_port_cleanup{bad_port_path};
    REQUIRE(widget.loadFlow(bad_port_path.string()));
    const TestFlowWidget::FlowPreview bad_port = widget.preview();
    REQUIRE_FALSE(bad_port.runnable);
    REQUIRE(bad_port.measurements[0].component_resolved);
    REQUIRE_FALSE(bad_port.measurements[0].port_resolved);
    REQUIRE(bad_port.measurements[0].output_ports == amplifier.node().outputs.size());
    REQUIRE_FALSE(bad_port.issues.empty());
    // The unrun flow never produced rows, and an unresolved panel must not run.
    REQUIRE(widget.result().has_value() == false);
}

// ---------------------------------------------------------------------------
TEST_CASE("TestFlowWidget: the expected row count is a saturating product",
          "[test_flow][widget][panel]") {
    REQUIRE(TestFlowWidget::saturatingMultiply(0, 5) == 0);
    REQUIRE(TestFlowWidget::saturatingMultiply(3, 4) == 12);
    REQUIRE(TestFlowWidget::saturatingMultiply(1, 7) == 7);
    REQUIRE(TestFlowWidget::saturatingMultiply(std::numeric_limits<size_t>::max(), 2) ==
            std::numeric_limits<size_t>::max());
    REQUIRE(TestFlowWidget::saturatingMultiply(std::numeric_limits<size_t>::max() / 2, 3) ==
            std::numeric_limits<size_t>::max());
    REQUIRE(TestFlowWidget::saturatingMultiply(std::numeric_limits<size_t>::max() / 2, 2) ==
            std::numeric_limits<size_t>::max() - 1);

    FlowSpec none;
    REQUIRE(TestFlowWidget::expectedRowCount(none) == 1);

    FlowSpec two;
    two.conditions = {Condition{1, "a", {-1.0, 0.0, 1.0}}, Condition{2, "b", {0.0, 1.0, 2.0, 3.0}}};
    REQUIRE(TestFlowWidget::expectedRowCount(two) == 12);

    FlowSpec one;
    one.conditions = {Condition{1, "a", {-1.0}}};
    REQUIRE(TestFlowWidget::expectedRowCount(one) == 1);
}

// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture,
                 "TestFlowWidget: invalid and non-finite measurements render as N/A and stay null",
                 "[test_flow][widget][panel]") {
    MetricSample sample;
    sample.name = "power_dBm";
    sample.component = 1;
    sample.port = 0;
    sample.unit = "dBm";

    // An unusable reading renders as N/A whatever the value field holds.
    sample.valid = false;
    REQUIRE(TestFlowWidget::formatMetricValue(sample) == "N/A");

    sample.valid = true;
    sample.value = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(TestFlowWidget::formatMetricValue(sample) == "N/A");
    sample.value = std::numeric_limits<double>::infinity();
    REQUIRE(TestFlowWidget::formatMetricValue(sample) == "N/A");
    sample.value = -std::numeric_limits<double>::infinity();
    REQUIRE(TestFlowWidget::formatMetricValue(sample) == "N/A");

    sample.value = 12.5;
    const std::string rendered = TestFlowWidget::formatMetricValue(sample);
    REQUIRE(rendered.find("12.5") != std::string::npos);
    REQUIRE(rendered.find("dBm") != std::string::npos);

    // A real measurement with no data behind it: a circuit whose probe has no
    // spectrum at all, so the reading is invalid and the exported JSON encodes
    // its value as null (never NaN). These engines are local to the test, so the
    // expected validity cannot drift with a DSP engine's default output.
    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry components(graph, view);
    constexpr int kSweptId = 810;
    constexpr int kProbeId = 811;
    auto &swept = components.add<TrackingEngine>(kSweptId, graph, 1.0);
    auto &probe = components.add<TrackingEngine>(kProbeId, graph, 2.0);
    graph.addLink(swept.outputPinId(), probe.inputPinId());

    const fs::path path = uniqueTempPath("invalid_metric");
    writeText(path, singleValueFlowJson(kSweptId, kProbeId, 2.0));
    ScopedRemove cleanup{path};

    TestFlowWidget widget(components, graph);
    REQUIRE(widget.loadFlow(path.string()));
    REQUIRE(widget.preview().runnable);
    REQUIRE(widget.run());
    REQUIRE(widget.result().has_value());
    REQUIRE(widget.result()->ok);

    const FlowRow &row = widget.result()->rows[0];
    REQUIRE(row.metrics.size() == 1);
    REQUIRE_FALSE(row.metrics[0].valid);
    REQUIRE(TestFlowWidget::formatMetricValue(row.metrics[0]) == "N/A");

    const nlohmann::json json = widget.result()->toJson();
    const std::string key = std::to_string(kProbeId) + ":0:power_dBm";
    REQUIRE(json["rows"][0]["metrics"].contains(key));
    REQUIRE(json["rows"][0]["metrics"][key]["value"].is_null());
    REQUIRE(json["rows"][0]["metrics"][key]["valid"] == false);
}

// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "TestFlowWidget: export writes pretty JSON with a trailing newline",
                 "[test_flow][widget][panel][export]") {
    RfSimulatorApp app;
    SignalGeneratorEngine &generator =
        *app.testComponents().byType<SignalGeneratorEngine>().front();
    AmplifierEngine &amplifier = *app.testComponents().byType<AmplifierEngine>().front();
    app.testGraphEngine().addLink(generator.outputPinId(), amplifier.inputPinId());

    const fs::path path = uniqueTempPath("export_flow");
    writeText(path, sweepFlowJson(generator.id(), amplifier.id(), 3));
    ScopedRemove flow_cleanup{path};

    TestFlowWidget &widget = app.testTestFlowWidget();

    // Not run yet: nothing to export, and no file may be created.
    const fs::path early_path = uniqueTempPath("export_early");
    ScopedRemove early_cleanup{early_path};
    REQUIRE_FALSE(widget.exportResult(early_path.string()));
    REQUIRE_FALSE(fs::exists(early_path));

    REQUIRE(widget.loadFlow(path.string()));
    REQUIRE(widget.run());

    const std::vector<ComponentState> components_before = snapshotComponents(app.testComponents());
    const std::vector<std::array<int, 3>> links_before = snapshotLinks(app.testGraphEngine());
    const bool dirty_before = app.isDirty();

    const fs::path export_path = uniqueTempPath("export_results");
    ScopedRemove export_cleanup{export_path};
    REQUIRE(widget.exportResult(export_path.string()));
    REQUIRE(fs::exists(export_path));
    REQUIRE(readText(export_path) == widget.result()->toJson().dump(2) + "\n");

    // Exactly the pretty form the JSON encoder produces, still parseable.
    const nlohmann::json reloaded = nlohmann::json::parse(readText(export_path));
    REQUIRE(reloaded == widget.result()->toJson());
    REQUIRE(reloaded["rows"].size() == widget.result()->rows.size());

    // Exporting is a read of the in-memory result, never a project mutation.
    const std::vector<ComponentState> components_after = snapshotComponents(app.testComponents());
    REQUIRE(components_after.size() == components_before.size());
    for (size_t i = 0; i < components_before.size(); ++i)
        REQUIRE(components_after[i].state == components_before[i].state);
    REQUIRE(snapshotLinks(app.testGraphEngine()) == links_before);
    REQUIRE(app.isDirty() == dirty_before);
    REQUIRE(widget.result().has_value());
}

// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture,
                 "TestFlowWidget: a failed export keeps the result and reports the failure",
                 "[test_flow][widget][panel][export][failure]") {
    RfSimulatorApp app;
    SignalGeneratorEngine &generator =
        *app.testComponents().byType<SignalGeneratorEngine>().front();
    AmplifierEngine &amplifier = *app.testComponents().byType<AmplifierEngine>().front();
    app.testGraphEngine().addLink(generator.outputPinId(), amplifier.inputPinId());

    const fs::path path = uniqueTempPath("export_failure_flow");
    writeText(path, sweepFlowJson(generator.id(), amplifier.id(), 3));
    ScopedRemove cleanup{path};

    TestFlowWidget &widget = app.testTestFlowWidget();
    REQUIRE(widget.loadFlow(path.string()));
    REQUIRE(widget.run());
    REQUIRE(widget.result()->rows.size() == 3);

    const fs::path bad_path = uniqueMissingDirectoryPath("export");
    REQUIRE_FALSE(widget.exportResult(bad_path.string()));
    // The in-memory result survives a failed write, so the user can retry.
    REQUIRE(widget.result().has_value());
    REQUIRE(widget.result()->ok);
    REQUIRE(widget.result()->rows.size() == 3);
    REQUIRE(widget.statusMessage().find("Export failed") == 0);

    // The retry against a writable path still succeeds.
    const fs::path good_path = uniqueTempPath("export_retry");
    ScopedRemove retry_cleanup{good_path};
    REQUIRE(widget.exportResult(good_path.string()));
    REQUIRE(readText(good_path) == widget.result()->toJson().dump(2) + "\n");
}

// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture,
                 "TestFlowWidget: a circuit reload revalidates the selection it retains",
                 "[test_flow][widget][panel][restore]") {
    RfSimulatorApp app;
    SignalGeneratorEngine &generator =
        *app.testComponents().byType<SignalGeneratorEngine>().front();
    AmplifierEngine &amplifier = *app.testComponents().byType<AmplifierEngine>().front();
    app.testGraphEngine().addLink(generator.outputPinId(), amplifier.inputPinId());

    const fs::path path = uniqueTempPath("reload");
    writeText(path, sweepFlowJson(generator.id(), amplifier.id(), 3));
    ScopedRemove cleanup{path};

    TestFlowWidget &widget = app.testTestFlowWidget();
    REQUIRE(widget.loadFlow(path.string()));
    REQUIRE(widget.run());
    REQUIRE(widget.result()->rows.size() == 3);

    const std::string retained = widget.selectedPath();

    // newProject() replaces every engine, so the app resets the panel: the latch
    // and the stale result go, the selection stays.
    app.newProject();
    REQUIRE(widget.selectedPath() == retained);
    REQUIRE_FALSE(widget.restoreFailed());
    REQUIRE_FALSE(widget.result().has_value());
    REQUIRE(widget.statusMessage().empty());

    // The retained selection is revalidated against the new circuit before it is
    // allowed to run again: the swept component is gone.
    const TestFlowWidget::FlowPreview preview = widget.preview();
    REQUIRE(preview.loaded);
    REQUIRE_FALSE(preview.runnable);
    REQUIRE_FALSE(preview.conditions[0].resolved);
    REQUIRE_FALSE(preview.issues.empty());

    // Running anyway cannot touch the destroyed circuit — it just reports it.
    REQUIRE_FALSE(widget.run());
    REQUIRE(widget.result().has_value());
    REQUIRE_FALSE(widget.result()->ok);
    REQUIRE(widget.result()->error.code == FlowErrorCode::ComponentNotFound);
    REQUIRE(widget.result()->rows.empty());

    // A successful project load is the other reset point. The replacement
    // circuit needs its own components and a flow that addresses them — the
    // original engine ids are gone for good.
    const fs::path second_flow = uniqueTempPath("reload_second_flow");
    const fs::path project_path = uniqueTempPath("reload_project");
    ScopedRemove second_flow_cleanup{second_flow};
    ScopedRemove project_cleanup{project_path};

    SignalGeneratorEngine &second_generator =
        app.testComponents().add<SignalGeneratorEngine>(900, app.testGraphEngine());
    second_generator.addTone(100e6, -20.0);
    AmplifierEngine &second_amplifier =
        app.testComponents().add<AmplifierEngine>(901, app.testGraphEngine());
    app.testGraphEngine().addLink(second_generator.outputPinId(), second_amplifier.inputPinId());
    writeText(second_flow, sweepFlowJson(900, 901, 3));

    REQUIRE(widget.loadFlow(second_flow.string()));
    REQUIRE(widget.preview().runnable);
    REQUIRE(widget.run());
    REQUIRE(widget.result()->rows.size() == 3);
    app.saveProject(project_path.string());

    app.loadProject(project_path.string());
    REQUIRE_FALSE(widget.result().has_value());
    REQUIRE_FALSE(widget.restoreFailed());
    // The retained selection is revalidated against the reloaded circuit rather
    // than assumed valid: the loader re-mints component ids, so the flow above
    // no longer resolves.
    REQUIRE(widget.preview().loaded);
    REQUIRE_FALSE(widget.preview().runnable);

    // A failed load is not a reset: the app only resets after a successful load.
    SignalGeneratorEngine &restored_generator =
        *app.testComponents().byType<SignalGeneratorEngine>().front();
    AmplifierEngine &restored_amplifier = *app.testComponents().byType<AmplifierEngine>().front();
    writeText(second_flow, sweepFlowJson(restored_generator.id(), restored_amplifier.id(), 3));
    REQUIRE(widget.loadFlow(second_flow.string()));
    REQUIRE(widget.preview().runnable);
    REQUIRE(widget.run());
    REQUIRE(widget.result().has_value());
    const fs::path missing_project = uniqueTempPath("reload_missing_project");
    app.loadProject(missing_project.string());
    REQUIRE(widget.result().has_value());
}

// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture,
                 "TestFlowWidget: draw only invokes a callback for its clicked button",
                 "[test_flow][widget][panel][draw]") {
    RfSimulatorApp app;
    SignalGeneratorEngine &generator =
        *app.testComponents().byType<SignalGeneratorEngine>().front();
    AmplifierEngine &amplifier = *app.testComponents().byType<AmplifierEngine>().front();
    app.testGraphEngine().addLink(generator.outputPinId(), amplifier.inputPinId());

    TestFlowWidget &widget = app.testTestFlowWidget();

    int open_calls = 0;
    int export_calls = 0;
    const auto open_dialog = [&open_calls]() { ++open_calls; };
    const auto export_dialog = [&export_calls]() { ++export_calls; };

    // Merely drawing the panel opens no dialog: a canceled native dialog is
    // therefore a no-op, and nothing about the selection changes.
    const TestFlowWidget::ButtonRects rects =
        warmUpAndReadRects(widget, open_dialog, export_dialog);
    REQUIRE(open_calls == 0);
    REQUIRE(export_calls == 0);
    REQUIRE(widget.selectedPath().empty());

    // Export is disabled until a successful run, so clicking it does nothing.
    clickButton(widget, rects.export_button, open_dialog, export_dialog);
    REQUIRE(export_calls == 0);

    // Open always works and invokes only its own callback.
    clickButton(widget, rects.open, open_dialog, export_dialog);
    REQUIRE(open_calls == 1);
    REQUIRE(export_calls == 0);

    // An unresolved flow disables Run, so a click cannot even reach the runner
    // (an enabled click would have produced the ComponentNotFound result).
    const fs::path unknown_path = uniqueTempPath("draw_unknown");
    writeText(unknown_path, sweepFlowJson(987654, amplifier.id()));
    ScopedRemove unknown_cleanup{unknown_path};
    REQUIRE(widget.loadFlow(unknown_path.string()));
    REQUIRE_FALSE(widget.preview().runnable);
    const TestFlowWidget::ButtonRects unresolved_rects =
        warmUpAndReadRects(widget, open_dialog, export_dialog);
    clickButton(widget, unresolved_rects.run, open_dialog, export_dialog);
    REQUIRE_FALSE(widget.result().has_value());

    // With a successful run the Export button is enabled and invokes the export
    // callback; Run itself stays wired to the model.
    const fs::path good_path = uniqueTempPath("draw_good");
    writeText(good_path, sweepFlowJson(generator.id(), amplifier.id(), 3));
    ScopedRemove good_cleanup{good_path};
    REQUIRE(widget.loadFlow(good_path.string()));
    const TestFlowWidget::ButtonRects runnable_rects =
        warmUpAndReadRects(widget, open_dialog, export_dialog);
    clickButton(widget, runnable_rects.run, open_dialog, export_dialog);
    REQUIRE(widget.result().has_value());
    REQUIRE(widget.result()->ok);

    const TestFlowWidget::ButtonRects result_rects =
        warmUpAndReadRects(widget, open_dialog, export_dialog);
    clickButton(widget, result_rects.export_button, open_dialog, export_dialog);
    REQUIRE(export_calls == 1);
    REQUIRE(open_calls == 1);
}
