#pragma once

#include "flow_result.h"
#include "flow_runner.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

class ComponentRegistry;
class NodeGraphEngine;

// App-owned model + panel for running an existing test-flow file against the
// live circuit.
//
// The runner deliberately drives the user's *real* components: RunFlow()
// deserializes each swept value into the live engines. Without a boundary that
// would silently rewrite the project's parameter values with the last sweep
// value. run() therefore snapshots serialize() for every engine in
// ComponentRegistry::all() before execution, restores every snapshot afterwards
// (each restore attempt is independent, so one failure cannot strand the rest),
// and then runs the shared rewireComponentInputs() pass so input pointers match
// the untouched graph. The widget never touches the app's dirty flag.
//
// A restoration failure latches restoreFailed(): the run reports failure, later
// runs are refused, and the user recovers by reloading the circuit, after which
// RfSimulatorApp calls resetAfterCircuitReload(). The latch also keeps the
// original failure notice in statusMessage() across every later loadFlow(), so
// the panel never sits blocked without stating that a reload is required.
//
// The widget holds ComponentRegistry and NodeGraphEngine references only — it
// never knows about RfSimulatorApp, and dialog code lives in the app lambdas
// draw() invokes.
class TestFlowWidget {
  public:
    TestFlowWidget(ComponentRegistry &components, NodeGraphEngine &graph);

    // Replaces the selection and clears the previous result before parsing, so
    // a failed load can never leave stale rows behind. Returns false when the
    // flow file is unreadable or invalid. While restoreFailed() is true the
    // status also keeps the latched restoration-failure notice, next to this
    // load's own error if it has one.
    bool loadFlow(const std::string &path);

    // Runs the loaded flow inside the snapshot/restore boundary. True only when
    // execution, every measurement, and every restoration succeeded.
    bool run();

    // Writes the last successful result as pretty JSON plus a trailing newline.
    // Unavailable until a successful run; a failure is reported through
    // statusMessage() and never discards the in-memory result.
    bool exportResult(const std::string &path);

    // Renders the panel. The callbacks are invoked only when their buttons are
    // clicked, and a canceled native dialog never re-enters the model — the app
    // lambdas pass a chosen path to loadFlow()/exportResult() themselves.
    void draw(const char *title, bool *open, const std::function<void()> &open_dialog,
              const std::function<void()> &export_dialog);

    const std::string &selectedPath() const { return m_selected_path; }
    const FlowLoadResult &loadState() const { return m_load_state; }
    const std::optional<FlowResult> &result() const { return m_result; }
    // While restoreFailed() is true this always includes the latched
    // restoration-failure notice, whatever the last load attempt reported.
    const std::string &statusMessage() const { return m_status; }
    bool restoreFailed() const { return m_restore_failed; }

    // One swept input as the panel renders it. `component` is the flow's
    // IComponentEngine::id(); it is resolved by iterating
    // ComponentRegistry::all() and matching IComponentEngine::id() — never
    // ComponentRegistry::find(), which is keyed by graph-node id and would
    // resolve the wrong engine (or none). `values` is a bounded preview.
    struct ConditionEntry {
        int component = -1;
        std::string path;
        size_t value_count = 0;
        std::vector<double> values;
        bool resolved = false;
        std::string component_label;
    };

    // One output reading as the panel renders it. `port_resolved` is true only
    // when the component resolved and its node() actually has that output port.
    struct MeasurementEntry {
        int component = -1;
        int port = 0;
        std::string metric;
        bool component_resolved = false;
        bool port_resolved = false;
        size_t output_ports = 0;
        std::string component_label;
    };

    // The loaded flow validated against the live circuit, recomputed on demand
    // because a project load or new project replaces every engine. `runnable`
    // is false for an unloaded flow, an unresolved id/port, or a latched
    // restoration failure — the panel disables Run on it.
    struct FlowPreview {
        bool loaded = false;
        std::string name;
        std::vector<ConditionEntry> conditions;
        std::vector<MeasurementEntry> measurements;
        size_t expected_rows = 0;
        bool runnable = false;
        std::vector<std::string> issues;
    };

    FlowPreview preview() const;

    // {x0, y0, x1, y1} of the last draw()'s buttons. A headless frame has no
    // label-based item lookup without the ImGui Test Engine, so the panel tests
    // press at these rects to drive a real click.
    struct ButtonRects {
        float open[4] = {};
        float run[4] = {};
        float export_button[4] = {};
    };
    const ButtonRects &lastButtonRects() const { return m_button_rects; }

    // Text of one result-table metric cell: "N/A" when the sample is invalid or
    // its value is not finite, otherwise the value and unit. The table therefore
    // shows N/A in exactly the rows the exported JSON encodes as null.
    static std::string formatMetricValue(const MetricSample &sample);

    // One row per FlowRow: a flow with no conditions has a single row, otherwise
    // the product of the condition value counts — saturating, never wrapping.
    static size_t expectedRowCount(const FlowSpec &spec);
    static size_t saturatingMultiply(size_t left, size_t right);

    // Clears the restoration latch, its notice, and the stale result. The app
    // calls this only after newProject() or a successful project load, because
    // those replace every engine the widget's snapshots referred to. This is
    // the only path that clears either.
    void resetAfterCircuitReload();

  private:
    ComponentRegistry &m_components;
    NodeGraphEngine &m_graph;
    std::string m_selected_path;
    FlowLoadResult m_load_state;
    std::optional<FlowResult> m_result;
    std::string m_status;
    bool m_restore_failed = false;
    // The exact message set when the latch was taken; re-stated by every later
    // status write that would otherwise hide why the panel is blocked. Empty
    // exactly while the latch is clear.
    std::string m_restore_failure;
    ButtonRects m_button_rects;
};
