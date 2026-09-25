#pragma once

#include "flow_author.h"
#include "flow_params.h"
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
//
// Whether a flow applies to the circuit is never the panel's own opinion:
// preview() maps ValidateFlow()'s issues onto the panel, so Run cannot be
// enabled for a flow RunFlow() would refuse (a condition `path` addresses
// serialize() keys, not inspector labels, which makes it the likeliest
// hand-authoring mistake). ValidateFlow() is exhaustive and resolves each
// condition's path once, so calling it every frame stays affordable.
//
// The panel also authors flows (issue #155), because writing the file by hand
// meant reverse-engineering each engine's serialize() keys and the circuit's
// current ids. Two rules keep that from becoming a second, divergent opinion:
//
//   * Every reference the authoring form offers is read from the live circuit —
//     component ids from ComponentRegistry::all(), paths from
//     describeConditionPaths() over that engine's own serialize() — so an
//     invented component or key cannot be authored at all.
//   * Edits go into an in-tool draft, and spec() is the single accessor
//     preview(), run() and saveFlow() read: what the panel validates, runs and
//     writes is one FlowSpec. The JSON document shape and the value-text grammar
//     come from test_flow/flow_author.h, so the harness keeps its no-ImGui link
//     rule and the file the panel writes is the file its loader reads.
class TestFlowWidget {
  public:
    TestFlowWidget(ComponentRegistry &components, NodeGraphEngine &graph);

    // Replaces the selection and clears the previous result before parsing, so
    // a failed load can never leave stale rows behind. Returns false when the
    // flow file is unreadable or invalid. An in-tool draft is discarded (the file
    // is the flow again), and the status states that rather than dropping unsaved
    // edits silently; while restoreFailed() is true it also keeps the latched
    // restoration-failure notice, next to this load's own error if it has one.
    bool loadFlow(const std::string &path);

    // Runs the loaded flow inside the snapshot/restore boundary. True when the
    // execution completes without a fatal FlowResult error and every snapshot
    // restoration succeeded. An individual measurement is not part of that
    // condition: an invalid MetricSample (`valid == false`) is a normal result,
    // preserved and rendered as "N/A" (JSON null), and never fails the run.
    // Refused, without touching the circuit, while restoreFailed() is true, when
    // no valid flow is loaded, or when the sweep exceeds kMaxRunRows.
    bool run();

    // Writes the last successful result as pretty JSON plus a trailing newline.
    // Unavailable until a successful run; a failure is reported through
    // statusMessage() and never discards the in-memory result.
    bool exportResult(const std::string &path);

    // Renders the panel. The callbacks are invoked only when their buttons are
    // clicked, and a canceled native dialog never re-enters the model — the app
    // lambdas pass a chosen path to loadFlow()/exportResult()/saveFlow()
    // themselves. `save_dialog` may be empty (the button then does nothing), which
    // keeps a caller that only wants to run flows from having to supply it.
    void draw(const char *title, bool *open, const std::function<void()> &open_dialog,
              const std::function<void()> &export_dialog,
              const std::function<void()> &save_dialog = {});

    const std::string &selectedPath() const { return m_selected_path; }
    const FlowLoadResult &loadState() const { return m_load_state; }
    const std::optional<FlowResult> &result() const { return m_result; }
    // While restoreFailed() is true this always includes the latched
    // restoration-failure notice, whatever the last load attempt reported.
    const std::string &statusMessage() const { return m_status; }
    bool restoreFailed() const { return m_restore_failed; }

    // -----------------------------------------------------------------------
    // Authoring (issue #155)
    // -----------------------------------------------------------------------

    // The one spec preview(), run() and saveFlow() read: the in-tool draft when
    // one exists, otherwise the loaded file's. Empty-valued when there is
    // neither (check hasSpec()).
    const FlowSpec &spec() const;
    bool hasSpec() const;
    // True while the panel holds edits that are not (yet) in the selected file:
    // either a draft seeded from the circuit or a loaded flow edited in place.
    // Saving writes it and loads it straight back, so this clears on success —
    // and so does a load or reload, which discards the draft and says so in the
    // status. A *refused* edit never creates one.
    bool draftActive() const { return m_draft.has_value(); }

    // One serialize() key the authoring form can offer for a component, plus
    // whether the draft already conditions on it — the marker that turns "why is
    // this flow not running" into "you already sweep that".
    struct AuthoringPath {
        std::string path;
        std::string type_name;
        bool numeric = false;
        double value = 0.0;
        bool used = false;
    };

    // One component the form can target, with every key its serialize() exposes.
    // `component` is IComponentEngine::id(); the label is that id with the engine
    // type, because the id alone is what the flow file stores.
    struct AuthoringComponent {
        int component = -1;
        std::string label;
        std::vector<AuthoringPath> paths;
    };

    // Every component in the live circuit, in registry order, each with its
    // discoverable paths (describeConditionPaths()). Components whose snapshot
    // exposes no scalar leaf are still listed with an empty `paths`, so the form
    // can say why they cannot be swept instead of hiding them.
    std::vector<AuthoringComponent> authoringComponents() const;

    // Seeds the draft from the loaded circuit: a first measurement on the first
    // component with an output port (the one thing a flow cannot do without), and
    // the authoring form pre-filled with the first component that exposes a
    // sweepable key and that key's *current* value — so one click on Add condition
    // completes a flow that sweeps the circuit from where it already is. The sweep
    // itself is left to the author: no key is guessed into the flow. Every id and
    // path is the circuit's. Returns false, with a status, when there is nothing
    // to measure — in which case no draft is created and any previous draft is
    // left alone.
    bool newFlowFromCircuit();

    // Edits the draft, starting one from the loaded spec (or an empty spec when
    // nothing is loaded) if needed. Each refuses, with a status message and
    // without touching the draft, anything the harness could not run: an unknown
    // component id, a path that does not resolve to a numeric slot on that
    // component, an empty value list, a value the slot rejects (the message is
    // the harness's own, so the form and the pre-flight explain a mistake the
    // same way), or a duplicate (component, path) target — which the loader
    // refuses but ValidateFlow() does not, so allowing it here would let the
    // panel run a flow whose own saved file could not be loaded back.
    bool addCondition(int component, const std::string &path, const std::vector<double> &values);
    // Replaces one condition wholesale. `index` is exempt from the duplicate check
    // against itself; everything else is checked as for addCondition().
    bool updateCondition(size_t index, int component, const std::string &path,
                         const std::vector<double> &values);
    bool removeCondition(size_t index);

    bool addMeasurement(int component, int port, const std::string &metric);
    bool removeMeasurement(size_t index);

    // Writes spec() as a flow file and loads it straight back, so the panel then
    // holds exactly what the file holds (and the loader has verified it). False,
    // with a status, on a write failure or when the written file does not parse.
    bool saveFlow(const std::string &path);
    // Re-reads the selected file from disk, so a hand edit can be iterated on
    // inside the app. False with a status when nothing is selected or the file no
    // longer parses; an in-tool draft is discarded by this, exactly as a fresh
    // load discards it, and the status says so, because the file is then the truth
    // again and the discard must not be silent.
    bool reloadFlow();

    // -----------------------------------------------------------------------
    // The authoring form (one component + serialize() key + value list at a time)
    // -----------------------------------------------------------------------

    // The form's target component, or -1 before one is picked.
    int authoringComponent() const { return m_form_component; }
    // The form's selected path, empty before one is picked.
    const std::string &authoringPath() const { return m_form_path; }
    // The form's values field, exactly as the author typed it. The model is the
    // storage of record: draw() renders it through a scratch ImGui buffer and
    // writes every keystroke back here, so a headless test drives the same text.
    const std::string &authoringValuesText() const { return m_form_values; }
    // The condition the form is editing, or -1 when the next commit appends a new
    // one.
    int authoringEditIndex() const { return m_form_edit_index; }
    // The last form problem (a value list that does not parse, say), cleared as
    // soon as the form is used again. Empty when the form is fine.
    const std::string &authoringError() const { return m_form_error; }

    // Targets a component and clears the path and value selection: a path belongs
    // to one component's snapshot, so it cannot survive the switch. `component`
    // must be a registry id; -1 clears the form.
    void setAuthoringComponent(int component);
    // Selects a serialize() path and seeds the values field with that path's
    // current value — "sweep this key from where it is now", which is what makes
    // a scaffolded flow runnable without typing a number. False (with an
    // authoringError()) when the path does not resolve to a numeric slot on the
    // form's component.
    bool setAuthoringPath(const std::string &path);
    // Replaces the values field's contents. False (with an authoringError())
    // when the text is not a value list, in which case the text is kept so the
    // author can correct it rather than silently dropping their edit.
    bool setAuthoringValuesText(const std::string &text);
    // Loads an existing condition into the form so the next commit updates it in
    // place instead of appending a second sweep.
    bool beginEditingCondition(size_t index);
    void cancelEditingCondition();
    // Commits the form: update when a condition is being edited, append otherwise.
    // Validates exactly as addCondition()/updateCondition() do.
    bool commitAuthoringForm();

    // The measurement form's selection (what the Add button would use), and its
    // commit. Measurements are added and removed rather than edited in place:
    // removing one and adding the corrected reading is the same edit with no
    // second piece of form state to keep in sync.
    int authoringMeasurementComponent() const { return m_form_meas_component; }
    int authoringMeasurementPort() const { return m_form_meas_port; }
    const std::string &authoringMeasurementMetric() const { return m_form_meas_metric; }
    void setAuthoringMeasurement(int component, int port, const std::string &metric);
    bool commitAuthoringMeasurement();

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
        // True only when the path itself resolves to a numeric slot on the
        // resolved component. The preview checks this by the same call the
        // pre-flight makes, so the table cannot show a path as fine that Run is
        // about to refuse.
        bool path_resolved = false;
        // When the path does not resolve, the keys that component's serialize()
        // does expose (describeConditionPaths(), numeric ones first, bounded by
        // kMaxPathHints) — the answer to "so what *is* the key called?".
        std::vector<std::string> path_hints;
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

    // The most sweep rows the panel will run. A flow above it is refused by both
    // preview() (as an issue, so Run is disabled with a visible reason) and
    // run() — the cartesian product is executed synchronously on the UI thread,
    // so an unbounded sweep is a frozen window rather than a long wait. Raise it
    // only together with off-thread execution.
    static constexpr size_t kMaxRunRows = 10000;

    // How many of a component's serialize() keys a single unresolved condition
    // lists as a hint. A tone-list-heavy engine can expose hundreds (one per
    // tone, per field), and the point of the hint is to jog the memory of
    // someone who mistyped one key, not to print a catalogue.
    static constexpr size_t kMaxPathHints = 12;

    // The loaded flow validated against the live circuit, recomputed on demand
    // because a project load or new project replaces every engine. `issues` is
    // the harness's own verdict list (ValidateFlow(), exhaustive) plus the row
    // limit; `runnable` is false for an unloaded flow, any issue, or a latched
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
        float reload[4] = {};
        float new_from_circuit[4] = {};
        float save_flow[4] = {};
        float add_condition[4] = {};
        float cancel_edit[4] = {};
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
    // Appends a status line, keeping the latched restoration notice visible: the
    // latch outlives every selection and result, so a status that dropped it
    // would leave the panel blocked with no stated reason.
    void setStatus(const std::string &message);
    // The draft, copied from the effective spec on the first edit. Engines can be
    // replaced under it (a project load), which is why preview() revalidates the
    // whole spec every frame instead of trusting what was true when it was built.
    FlowSpec &mutableDraft();
    // The component a flow id resolves to, or nullptr. Registry ids are the
    // flow's addressing space; graph node ids are not.
    IComponentEngine *componentById(int component) const;
    // addCondition()'s body, with `editing` exempt from the duplicate-target check.
    bool placeCondition(std::optional<size_t> editing, int component, const std::string &path,
                        const std::vector<double> &values);
    // The Authoring section draw() renders: the component/key/values form for a
    // condition, and the component/port/metric form for a measurement. Everything
    // it offers comes from authoringComponents() and the metric registry, so the
    // panel can only ever name something the circuit and the harness know.
    void drawAuthoringForms();

    ComponentRegistry &m_components;
    NodeGraphEngine &m_graph;
    std::string m_selected_path;
    FlowLoadResult m_load_state;
    // The in-tool draft (issue #155): set by newFlowFromCircuit() or the first
    // authoring edit, cleared by every load and by saveFlow()'s reload. While it
    // exists it is the effective spec, so the panel shows what the author is
    // working on rather than what the file on disk holds.
    std::optional<FlowSpec> m_draft;
    std::optional<FlowResult> m_result;
    std::string m_status;
    bool m_restore_failed = false;
    // The exact message set when the latch was taken; re-stated by every later
    // status write that would otherwise hide why the panel is blocked. Empty
    // exactly while the latch is clear.
    std::string m_restore_failure;

    // The authoring form's selection, held as an id and a path rather than as
    // indices into authoringComponents(): a project edit changes those lists, and
    // an id that is still present must not silently retarget to a neighbour.
    int m_form_component = -1;
    std::string m_form_path;
    std::string m_form_values;
    int m_form_edit_index = -1;
    std::string m_form_error;
    // The values field's ImGui buffer, grown from m_form_values (never shrunk)
    // instead of a fixed cap: a fixed 256-byte buffer silently truncated a long
    // sweep on the next keystroke and stored the truncated text back.
    std::vector<char> m_values_buffer;

    // The measurement form's selection. The metric starts at the panel's default
    // so the Add button works as soon as a component is picked.
    int m_form_meas_component = -1;
    int m_form_meas_port = 0;
    std::string m_form_meas_metric;

    ButtonRects m_button_rects;
};
