#pragma once

#include "flow_result.h"
#include "flow_runner.h"

#include <functional>
#include <optional>
#include <string>

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
// RfSimulatorApp calls resetAfterCircuitReload().
//
// The widget holds ComponentRegistry and NodeGraphEngine references only — it
// never knows about RfSimulatorApp, and dialog code lives in the app lambdas
// draw() invokes.
class TestFlowWidget {
  public:
    TestFlowWidget(ComponentRegistry &components, NodeGraphEngine &graph);

    // Replaces the selection and clears the previous result before parsing, so
    // a failed load can never leave stale rows behind. Returns false when the
    // flow file is unreadable or invalid.
    bool loadFlow(const std::string &path);

    // Runs the loaded flow inside the snapshot/restore boundary. True only when
    // execution, every measurement, and every restoration succeeded.
    bool run();

    // Writes the last successful result as pretty JSON plus a trailing newline.
    // Unavailable until a successful run.
    bool exportResult(const std::string &path) const;

    // Renders the panel. The callbacks are invoked only when their buttons are
    // clicked, and a canceled native dialog never re-enters the model — the app
    // lambdas pass a chosen path to loadFlow()/exportResult() themselves.
    void draw(const char *title, bool *open, const std::function<void()> &open_dialog,
              const std::function<void()> &export_dialog);

    const std::string &selectedPath() const { return m_selected_path; }
    const FlowLoadResult &loadState() const { return m_load_state; }
    const std::optional<FlowResult> &result() const { return m_result; }
    const std::string &statusMessage() const { return m_status; }
    bool restoreFailed() const { return m_restore_failed; }

    // Clears the restoration latch and the stale result. The app calls this
    // only after newProject() or a successful project load, because those
    // replace every engine the widget's snapshots referred to.
    void resetAfterCircuitReload();

  private:
    ComponentRegistry &m_components;
    NodeGraphEngine &m_graph;
    std::string m_selected_path;
    FlowLoadResult m_load_state;
    std::optional<FlowResult> m_result;
    std::string m_status;
    bool m_restore_failed = false;
};
