#include "test_flow_widget.h"

#include "component_interface.h"
#include "component_registry.h"
#include "flow_author.h"
#include "flow_metrics.h"
#include "flow_params.h"
#include "imgui.h"
#include "node_graph_engine.h"
#include "rewire.h"
#include "signal_node.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

// Flow files address engines by IComponentEngine::id(). ComponentRegistry::find()
// is keyed by graph-node id, so it would resolve the wrong engine (or none) —
// every lookup here scans ComponentRegistry::all() instead.
IComponentEngine *findById(std::span<IComponentEngine *const> components, int id) {
    for (IComponentEngine *component : components) {
        if (component && component->id() == id)
            return component;
    }
    return nullptr;
}

// How many sweep values a condition row previews before it is truncated.
constexpr size_t kPreviewValues = 4;

// How many results rows fit the table before it scrolls (and how many are
// submitted per frame — the rest are clipped).
constexpr float kResultTableRows = 12.0f;

// The values text field's ImGui buffer grows from the model text (see
// drawAuthoringForms): this is the typing headroom kept past the text's end, so
// the field never silently truncates a long sweep. The repo does not link
// imgui_stdlib, so the field renders through a std::vector<char> copy.
constexpr size_t kValuesBufferHeadroom = 64;

// The metric a scaffolded measurement starts at, and the measurement form's
// default. "power_dBm" is the total-power measurement the GUI's own power meter
// reports; it is registered by flow_metrics.cpp. The registry's names() is
// unordered, so the default is named rather than taken from it.
constexpr const char *kDefaultMetric = "power_dBm";

const ImVec4 kIssueColor(1.0f, 0.45f, 0.45f, 1.0f);

void appendError(std::string &out, const std::string &detail) {
    if (!out.empty())
        out += "; ";
    out += detail;
}

// Appends a notice on its own line, so one status can carry a second fact (the
// latched restoration failure, or a draft a load discarded) without overwriting
// the first.
std::string appendLine(const std::string &text, const std::string &line) {
    if (text.empty())
        return line;
    return text + "\n" + line;
}

std::string formatValue(double value) {
    if (!std::isfinite(value))
        return "N/A";
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    return buffer;
}

// "a, b, c" with a trailing hint when the sweep has more values than the
// preview keeps.
std::string formatValueList(const std::vector<double> &values, size_t total) {
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0)
            out += ", ";
        out += formatValue(values[i]);
    }
    if (total > values.size())
        out += ", ...";
    return out;
}

void recordItemRect(float out[4]) {
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    out[0] = min.x;
    out[1] = min.y;
    out[2] = max.x;
    out[3] = max.y;
}

} // namespace

TestFlowWidget::TestFlowWidget(ComponentRegistry &components, NodeGraphEngine &graph)
    : m_components(components), m_graph(graph) {
    // The measurement form is usable as soon as a component is picked, so its
    // metric starts at the panel's default rather than empty.
    m_form_meas_metric = kDefaultMetric;
}

void TestFlowWidget::setStatus(const std::string &message) {
    m_status = message;
    if (m_restore_failed)
        m_status = appendLine(m_status, m_restore_failure);
}

const FlowSpec &TestFlowWidget::spec() const {
    // The draft wins while it exists: every edit goes there, and preview(), run()
    // and saveFlow() read this one accessor, so the panel can never validate one
    // spec and run another.
    if (m_draft)
        return *m_draft;
    return m_load_state.spec;
}

bool TestFlowWidget::hasSpec() const { return m_draft.has_value() || m_load_state.ok; }

FlowSpec &TestFlowWidget::mutableDraft() {
    if (!m_draft) {
        // The first edit copies whatever is effective — a loaded file's spec, or
        // an empty one when nothing is loaded — so an author who starts from a
        // file edits that flow rather than an empty document.
        m_draft = m_load_state.ok ? m_load_state.spec : FlowSpec{};
    }
    return *m_draft;
}

IComponentEngine *TestFlowWidget::componentById(int component) const {
    return findById(m_components.all(), component);
}

bool TestFlowWidget::loadFlow(const std::string &path) {
    // The new selection replaces the old one outright: a parse failure must
    // never leave the previous flow's rows on screen next to its error. Any
    // in-tool draft goes the same way — the file is the truth again — but that
    // is a discard of unsaved edits, so the status says so rather than letting it
    // disappear quietly.
    const bool had_draft = m_draft.has_value();
    m_selected_path = path;
    m_result.reset();
    m_status.clear();
    m_draft.reset();
    m_form_edit_index = -1;
    m_form_error.clear();
    m_load_state = LoadFlowFile(path);
    // The latch notice, when there is one, rides along with this load's own
    // error (or with the empty status of a clean load).
    std::string message = m_load_state.ok ? std::string() : m_load_state.error.message;
    if (had_draft)
        message = appendLine(message, "The in-tool edits were discarded: the file is the flow.");
    setStatus(message);
    return m_load_state.ok;
}

bool TestFlowWidget::reloadFlow() {
    if (m_selected_path.empty()) {
        setStatus("Nothing to reload: no flow file is selected.");
        return false;
    }
    // loadFlow() re-reads the same path, so a hand edit is picked up without
    // re-picking the file; the selection is unchanged by construction.
    return loadFlow(m_selected_path);
}

bool TestFlowWidget::run() {
    if (m_restore_failed) {
        // Re-state the original failure (which names the reload) rather than a
        // generic line: the latch, not this call, is why the run is refused.
        m_status = m_restore_failure;
        return false;
    }
    if (!hasSpec()) {
        setStatus("Load or author a flow before running.");
        return false;
    }
    // A sweep above the panel's budget would block the UI thread for minutes and
    // build a row vector the panel cannot render, so it is refused here rather
    // than discovered mid-run. preview() reports the same limit, so the disabled
    // Run button always has a reason on screen.
    if (expectedRowCount(spec()) > kMaxRunRows) {
        setStatus("Run refused: the sweep exceeds the panel's " + std::to_string(kMaxRunRows) +
                  "-row limit.");
        return false;
    }

    // 1. Snapshot every live engine. RunFlow() deserializes into the real
    //    components, so an engine that cannot even be captured cannot be
    //    restored afterwards — refuse the run instead of performing it
    //    unguarded.
    std::vector<std::pair<int, nlohmann::json>> snapshots;
    snapshots.reserve(m_components.size());
    try {
        for (IComponentEngine *component : m_components.all()) {
            if (!component)
                continue;
            snapshots.emplace_back(component->id(), component->serialize());
        }
    } catch (const std::exception &error) {
        m_result.reset();
        setStatus(std::string("Run refused: cannot snapshot the circuit (") + error.what() + ")");
        return false;
    } catch (...) {
        m_result.reset();
        setStatus("Run refused: cannot snapshot the circuit (unknown exception)");
        return false;
    }

    // 2. Execute. From here the circuit has been touched whatever happens, so
    //    the restore below is unconditional.
    FlowResult result;
    std::string execution_error;
    try {
        result = RunFlow(spec(), m_components.all(), m_graph);
    } catch (const std::exception &error) {
        execution_error = error.what();
    } catch (...) {
        execution_error = "unknown exception";
    }

    // 3. Restore every snapshot, independently: one deserialize() failure must
    //    not strand the components after it. The shared pass then re-points
    //    every input at the output its (untouched) graph link names, exactly as
    //    the app's own rewireInputs() does.
    std::string restore_error;
    for (const auto &snapshot : snapshots) {
        IComponentEngine *component = findById(m_components.all(), snapshot.first);
        if (!component) {
            appendError(restore_error,
                        "component " + std::to_string(snapshot.first) + ": no longer present");
            continue;
        }
        try {
            component->deserialize(snapshot.second);
        } catch (const std::exception &error) {
            appendError(restore_error,
                        "component " + std::to_string(snapshot.first) + ": " + error.what());
        } catch (...) {
            appendError(restore_error,
                        "component " + std::to_string(snapshot.first) + ": unknown exception");
        }
    }
    rewireComponentInputs(m_components.all(), m_graph);

    // 4. Classify. A restore failure outranks everything — the live circuit may
    //    no longer match the project — and latches until a circuit reload. An
    //    execution exception is a run failure. An ordinary FlowResult error is
    //    the user's diagnostic and is kept, because restoration succeeded.
    if (!restore_error.empty()) {
        m_result.reset();
        // Store the notice before taking the latch: later status writes read it
        // back so the reason for the blocked panel survives new selections.
        m_restore_failure =
            "Restore failed: " + restore_error + " — reload the circuit to run flows again.";
        m_status = m_restore_failure;
        m_restore_failed = true;
        return false;
    }
    if (!execution_error.empty()) {
        m_result.reset();
        setStatus("Execution failed: " + execution_error);
        return false;
    }
    m_result = result;
    if (!result.ok) {
        setStatus(result.error.message);
        return false;
    }
    setStatus("Flow completed: " + std::to_string(result.rows.size()) + " row(s).");
    return true;
}

bool TestFlowWidget::exportResult(const std::string &path) {
    // The result stays in memory whatever the write does: a failed export is a
    // report, not a discard, so the user can pick another path and retry.
    if (m_restore_failed || !m_result || !m_result->ok) {
        setStatus("Export unavailable: no successful run to export.");
        return false;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        setStatus("Export failed: cannot write '" + path + "'.");
        return false;
    }
    out << m_result->toJson().dump(2) << '\n';
    out.flush();
    if (!out) {
        out.close();
        setStatus("Export failed: write error on '" + path + "'.");
        return false;
    }
    out.close();
    if (!out.good()) {
        setStatus("Export failed: close error on '" + path + "'.");
        return false;
    }
    setStatus("Exported results to '" + path + "'.");
    return true;
}

std::string TestFlowWidget::formatMetricValue(const MetricSample &sample) {
    if (!sample.valid || !std::isfinite(sample.value))
        return "N/A";
    std::string text = formatValue(sample.value);
    if (!sample.unit.empty()) {
        text += ' ';
        text += sample.unit;
    }
    return text;
}

size_t TestFlowWidget::saturatingMultiply(size_t left, size_t right) {
    if (left == 0 || right == 0)
        return 0;
    if (left > std::numeric_limits<size_t>::max() / right)
        return std::numeric_limits<size_t>::max();
    return left * right;
}

size_t TestFlowWidget::expectedRowCount(const FlowSpec &spec) {
    // No conditions is a single measurement pass, not zero rows.
    size_t rows = 1;
    for (const Condition &condition : spec.conditions)
        rows = saturatingMultiply(rows, condition.values.size());
    return rows;
}

// ---------------------------------------------------------------------------
// Authoring (issue #155)
// ---------------------------------------------------------------------------

std::vector<TestFlowWidget::AuthoringComponent> TestFlowWidget::authoringComponents() const {
    const FlowSpec &flow = spec();
    std::vector<AuthoringComponent> out;
    for (IComponentEngine *component : m_components.all()) {
        if (!component)
            continue;
        AuthoringComponent entry;
        entry.component = component->id();
        // The id is what a flow file stores; the type name is what makes it
        // recognisable, because two attenuators differ only by their id.
        entry.label = std::to_string(entry.component) + " " + std::string(component->type_name());
        for (const ConditionPathInfo &path : describeConditionPaths(component->serialize())) {
            AuthoringPath offer;
            offer.path = path.path;
            offer.type_name = path.type_name;
            offer.numeric = path.numeric;
            offer.value = path.value;
            for (const Condition &condition : flow.conditions) {
                if (condition.component == entry.component && condition.path == offer.path) {
                    offer.used = true;
                    break;
                }
            }
            entry.paths.push_back(std::move(offer));
        }
        out.push_back(std::move(entry));
    }
    return out;
}

bool TestFlowWidget::newFlowFromCircuit() {
    // A flow must measure something, and that is the one thing a scaffold cannot
    // leave to the author: the first component with an output port supplies it,
    // read from the registry — the id a flow file stores.
    int measure_component = -1;
    for (IComponentEngine *component : m_components.all()) {
        if (component && !component->node().outputs.empty()) {
            measure_component = component->id();
            break;
        }
    }
    if (measure_component < 0) {
        setStatus("New flow: this circuit has nothing to measure — no component has an output "
                  "port.");
        return false;
    }

    // The sweep itself stays the author's choice. The form is pre-filled with the
    // first component that exposes a sweepable key and that key's current value,
    // so one click on Add condition completes a flow that sweeps the circuit from
    // where it already is — and no key is guessed into the flow.
    int form_component = -1;
    ConditionPathInfo form_path;
    for (IComponentEngine *component : m_components.all()) {
        if (!component)
            continue;
        for (const ConditionPathInfo &path : describeConditionPaths(component->serialize())) {
            if (!path.numeric)
                continue;
            form_component = component->id();
            form_path = path;
            break;
        }
        if (form_component >= 0)
            break;
    }

    FlowSpec seeded;
    seeded.name = "from circuit";
    seeded.measure.push_back(Measurement{measure_component, 0, kDefaultMetric});

    // A scaffold is a new, unsaved flow: the previous file's identity and load
    // record no longer describe what the panel holds, and its result is stale.
    // A draft it replaces is unsaved work, so the status names that too.
    const bool replaced_draft = m_draft.has_value();
    m_selected_path.clear();
    m_load_state = FlowLoadResult{};
    m_result.reset();
    m_form_edit_index = -1;
    m_form_error.clear();
    m_draft = std::move(seeded);

    setAuthoringComponent(form_component >= 0 ? form_component : measure_component);
    if (form_component >= 0)
        setAuthoringPath(form_path.path);
    std::string message =
        "New flow seeded from the circuit: measuring component " +
        std::to_string(measure_component) + " port 0 with " + kDefaultMetric + "." +
        (form_component >= 0
             ? " " + form_path.path + " on component " + std::to_string(form_component) +
                   " is pre-filled with its current value — Add condition sweeps it."
             : " This circuit exposes no sweepable key.");
    if (replaced_draft)
        message = appendLine(message, "The previous in-tool edits were replaced.");
    setStatus(message);
    return true;
}

void TestFlowWidget::setAuthoringComponent(int component) {
    m_form_component = component;
    m_form_path.clear();
    m_form_values.clear();
    m_form_error.clear();
}

bool TestFlowWidget::setAuthoringPath(const std::string &path) {
    IComponentEngine *component = componentById(m_form_component);
    if (!component) {
        m_form_error = "Pick a component before picking a key.";
        return false;
    }
    // Resolve through the harness's own call: this both rejects a key the slot
    // cannot take and yields the current value to seed the field with.
    ConditionSlotKind kind = ConditionSlotKind::Float;
    std::string error;
    if (!resolveConditionSlot(component->serialize(), path, &kind, &error)) {
        m_form_error = error;
        return false;
    }
    double current = 0.0;
    for (const ConditionPathInfo &candidate : describeConditionPaths(component->serialize())) {
        if (candidate.path == path && candidate.numeric) {
            current = candidate.value;
            break;
        }
    }
    m_form_path = path;
    m_form_values = formatConditionValues({current});
    m_form_error.clear();
    return true;
}

bool TestFlowWidget::setAuthoringValuesText(const std::string &text) {
    // The field is the author's text, kept verbatim even when it does not parse:
    // a rejected keystroke that erased itself would be worse than a red note.
    m_form_values = text;
    std::vector<double> parsed;
    if (!parseConditionValues(text, &parsed, &m_form_error))
        return false;
    m_form_error.clear();
    return true;
}

bool TestFlowWidget::beginEditingCondition(size_t index) {
    if (index >= spec().conditions.size()) {
        setStatus("No such condition to edit.");
        return false;
    }
    const Condition condition = spec().conditions[index];
    setAuthoringComponent(condition.component);
    if (!setAuthoringPath(condition.path)) {
        // The path no longer resolves (a circuit edit since the flow was written);
        // keep the text so the author can see what the flow says.
        m_form_path = condition.path;
    }
    // The row's own values win over the seed setAuthoringPath() just took from the
    // circuit: editing starts from what the flow says.
    m_form_values = formatConditionValues(condition.values);
    m_form_edit_index = static_cast<int>(index);
    m_form_error.clear();
    return true;
}

void TestFlowWidget::cancelEditingCondition() {
    m_form_edit_index = -1;
    m_form_error.clear();
}

bool TestFlowWidget::placeCondition(std::optional<size_t> editing, int component,
                                    const std::string &path, const std::vector<double> &values) {
    if (values.empty()) {
        setStatus("A condition needs at least one value.");
        return false;
    }

    // The candidate is checked by the harness's own pre-flight, on its own, so a
    // refusal here is exactly the verdict Run would reach later — same rule, same
    // wording — and a value the slot cannot take is caught at the edit instead of
    // being authored into a flow that cannot run.
    FlowSpec candidate;
    candidate.conditions.push_back(Condition{component, path, values});
    const std::vector<FlowError> issues = ValidateFlow(candidate, m_components.all());
    if (!issues.empty()) {
        setStatus(issues.front().message);
        return false;
    }

    // Duplicate targets are the loader's rule (DuplicateConditionTarget), not
    // ValidateFlow()'s, so it is checked here: a draft the panel runs must also be
    // a file the loader accepts, and two conditions on one key would collide.
    // This runs against the *effective* spec before any draft exists, so a
    // refused edit cannot leave one behind — a refusal must not flip the panel
    // into "unsaved edits" for a flow the author never changed.
    const std::vector<Condition> &existing_conditions = spec().conditions;
    for (size_t i = 0; i < existing_conditions.size(); ++i) {
        if (editing && *editing == i)
            continue;
        if (existing_conditions[i].component == component && existing_conditions[i].path == path) {
            setStatus("Component " + std::to_string(component) + " already sweeps '" + path +
                      "' — edit that condition instead of adding a second one.");
            return false;
        }
    }

    FlowSpec &draft = mutableDraft();
    const bool updating = editing.has_value() && *editing < draft.conditions.size();
    if (updating)
        draft.conditions[*editing] = Condition{component, path, values};
    else
        draft.conditions.push_back(Condition{component, path, values});

    setStatus((updating ? "Condition updated: " : "Condition added: ") + std::to_string(component) +
              " " + path + " (" + std::to_string(values.size()) +
              (values.size() == 1 ? " value)." : " values)."));
    return true;
}

bool TestFlowWidget::addCondition(int component, const std::string &path,
                                  const std::vector<double> &values) {
    return placeCondition(std::nullopt, component, path, values);
}

bool TestFlowWidget::updateCondition(size_t index, int component, const std::string &path,
                                     const std::vector<double> &values) {
    if (index >= spec().conditions.size()) {
        setStatus("No such condition to update.");
        return false;
    }
    return placeCondition(index, component, path, values);
}

bool TestFlowWidget::removeCondition(size_t index) {
    if (index >= spec().conditions.size()) {
        setStatus("No such condition to remove.");
        return false;
    }
    FlowSpec &draft = mutableDraft();
    draft.conditions.erase(draft.conditions.begin() + static_cast<std::ptrdiff_t>(index));
    // The form's edit target is an index; keep it pointing at the same row after
    // the erase, or leave edit mode when that row was the one removed.
    if (m_form_edit_index == static_cast<int>(index))
        m_form_edit_index = -1;
    else if (m_form_edit_index > static_cast<int>(index))
        --m_form_edit_index;
    setStatus("Condition removed.");
    return true;
}

bool TestFlowWidget::addMeasurement(int component, int port, const std::string &metric) {
    FlowSpec candidate;
    candidate.measure.push_back(Measurement{component, port, metric});
    const std::vector<FlowError> issues = ValidateFlow(candidate, m_components.all());
    if (!issues.empty()) {
        setStatus(issues.front().message);
        return false;
    }

    // Checked against the effective spec, like the condition duplicate rule, so a
    // refusal happens before any draft is created.
    for (const Measurement &existing : spec().measure) {
        if (existing.component == component && existing.port == port && existing.metric == metric) {
            setStatus("Component " + std::to_string(component) + " already reports '" + metric +
                      "' at port " + std::to_string(port) + "'.");
            return false;
        }
    }

    FlowSpec &draft = mutableDraft();
    draft.measure.push_back(Measurement{component, port, metric});
    setStatus("Measurement added: " + std::to_string(component) + " port " + std::to_string(port) +
              " " + metric + ".");
    return true;
}

bool TestFlowWidget::removeMeasurement(size_t index) {
    if (index >= spec().measure.size()) {
        setStatus("No such measurement to remove.");
        return false;
    }
    FlowSpec &draft = mutableDraft();
    draft.measure.erase(draft.measure.begin() + static_cast<std::ptrdiff_t>(index));
    setStatus("Measurement removed.");
    return true;
}

bool TestFlowWidget::commitAuthoringForm() {
    if (m_form_component < 0) {
        m_form_error = "Pick a component first.";
        setStatus(m_form_error);
        return false;
    }
    if (m_form_path.empty()) {
        m_form_error = "Pick a serialize() key first.";
        setStatus(m_form_error);
        return false;
    }
    std::vector<double> values;
    if (!parseConditionValues(m_form_values, &values, &m_form_error)) {
        setStatus(m_form_error);
        return false;
    }

    const std::optional<size_t> editing =
        m_form_edit_index >= 0 ? std::optional<size_t>(static_cast<size_t>(m_form_edit_index))
                               : std::nullopt;
    if (!placeCondition(editing, m_form_component, m_form_path, values)) {
        // Also surface it beside the field: a form-level refusal belongs where the
        // author is looking, and the status line carries the same wording.
        m_form_error = m_status;
        return false;
    }
    m_form_error.clear();
    // A committed update leaves edit mode; an append leaves the form as it is, so
    // a component that needs two sweeps does not have to be picked twice.
    m_form_edit_index = -1;
    return true;
}

void TestFlowWidget::setAuthoringMeasurement(int component, int port, const std::string &metric) {
    m_form_meas_component = component;
    m_form_meas_port = port;
    m_form_meas_metric = metric;
}

bool TestFlowWidget::commitAuthoringMeasurement() {
    if (m_form_meas_component < 0) {
        setStatus("Pick a measurement component first.");
        return false;
    }
    return addMeasurement(m_form_meas_component, m_form_meas_port, m_form_meas_metric);
}

bool TestFlowWidget::saveFlow(const std::string &path) {
    if (!hasSpec()) {
        setStatus("Save unavailable: no flow to write.");
        return false;
    }
    // The panel only writes flows it could run, so the file on disk is always one
    // its own loader accepts and the pre-flight has nothing left to complain
    // about. `measure` is checked separately because an empty one is a loader
    // rule (EmptyMeasurement), not a ValidateFlow() one.
    if (spec().measure.empty()) {
        setStatus("Save refused: a flow needs at least one measurement.");
        return false;
    }
    const std::vector<FlowError> issues = ValidateFlow(spec(), m_components.all());
    if (!issues.empty()) {
        setStatus("Save refused: " + issues.front().message);
        return false;
    }

    std::string error;
    if (!writeFlowFile(path, buildFlowDocument(spec()), &error)) {
        setStatus("Save failed: " + error);
        return false;
    }
    // Load it straight back, so the panel holds exactly what the file holds and
    // the draft is cleared by the same path that cleared it for a fresh
    // selection. A written-but-unloadable file would be a bug here, and this
    // reports it rather than claiming a save that cannot be read.
    if (!loadFlow(path))
        return false;
    setStatus("Saved flow to '" + path + "'.");
    return true;
}

TestFlowWidget::FlowPreview TestFlowWidget::preview() const {
    FlowPreview out;
    if (!hasSpec())
        return out;
    const FlowSpec &flow = spec();
    out.loaded = true;
    out.name = flow.name;

    const std::span<IComponentEngine *const> components = m_components.all();

    for (const Condition &condition : flow.conditions) {
        ConditionEntry entry;
        entry.component = condition.component;
        entry.path = condition.path;
        entry.value_count = condition.values.size();
        const size_t shown = std::min(entry.value_count, kPreviewValues);
        entry.values.assign(condition.values.begin(), condition.values.begin() + shown);
        if (IComponentEngine *component = findById(components, condition.component)) {
            entry.resolved = true;
            entry.component_label = std::string(component->type_name());
            // The path is resolved by the very call the pre-flight makes, so the
            // table cannot show a path as fine that Run is about to refuse — and
            // a path that does not resolve can be answered with the keys the
            // engine does expose instead of only "it failed".
            const nlohmann::json snapshot = component->serialize();
            ConditionSlotKind kind = ConditionSlotKind::Float;
            std::string error;
            entry.path_resolved = resolveConditionSlot(snapshot, condition.path, &kind, &error);
            if (!entry.path_resolved) {
                for (const ConditionPathInfo &path : describeConditionPaths(snapshot)) {
                    if (!path.numeric)
                        continue;
                    entry.path_hints.push_back(path.path);
                    if (entry.path_hints.size() >= kMaxPathHints)
                        break;
                }
            }
        }
        out.conditions.push_back(std::move(entry));
    }

    for (const Measurement &measurement : flow.measure) {
        MeasurementEntry entry;
        entry.component = measurement.component;
        entry.port = measurement.port;
        entry.metric = measurement.metric;
        if (IComponentEngine *component = findById(components, measurement.component)) {
            entry.component_resolved = true;
            entry.component_label = std::string(component->type_name());
            entry.output_ports = component->node().outputs.size();
            entry.port_resolved =
                measurement.port >= 0 && static_cast<size_t>(measurement.port) < entry.output_ports;
        }
        out.measurements.push_back(std::move(entry));
    }

    // Every issue is the harness's own verdict, in RunFlow()'s order and
    // wording, so the panel can never call a flow runnable that RunFlow() would
    // refuse: a condition `path` addresses serialize() keys (never inspector
    // labels), and a typo — or a value the slot cannot take — there is the
    // mistake this pre-flight exists to catch before Run is enabled.
    for (const FlowError &error : ValidateFlow(flow, components))
        out.issues.push_back(error.message);

    out.expected_rows = expectedRowCount(flow);
    // A sweep above the panel's budget is refused by run() too; saying so here is
    // what keeps the disabled Run button explained.
    if (out.expected_rows > kMaxRunRows)
        out.issues.push_back("sweep exceeds the panel's " + std::to_string(kMaxRunRows) +
                             "-row limit");
    // A latched restoration failure outranks a resolvable flow: the live circuit
    // no longer matches the project, so nothing may run until a reload.
    out.runnable = out.issues.empty() && !m_restore_failed;
    return out;
}

void TestFlowWidget::draw(const char *title, bool *open, const std::function<void()> &open_dialog,
                          const std::function<void()> &export_dialog,
                          const std::function<void()> &save_dialog) {
    if (open && !*open)
        return;
    if (!ImGui::Begin(title, open)) {
        ImGui::End();
        return;
    }
    m_button_rects = ButtonRects{};

    if (m_selected_path.empty())
        ImGui::TextUnformatted(draftActive() ? "Flow: authored in the panel (unsaved)."
                                             : "No flow selected.");
    else
        ImGui::TextWrapped("Flow: %s", m_selected_path.c_str());
    if (!m_status.empty())
        ImGui::TextWrapped("%s", m_status.c_str());

    const FlowPreview state = preview();
    if (state.loaded)
        ImGui::Text("Name: %s", state.name.c_str());

    // Each callback runs only for its own clicked button: a canceled native
    // dialog is a no-op because the app lambda never entered the model.
    if (ImGui::Button("Open Flow...")) {
        if (open_dialog)
            open_dialog();
    }
    recordItemRect(m_button_rects.open);

    ImGui::SameLine();
    // Reload re-reads the selected file, so a hand edit outside the app can be
    // picked up without going through the open dialog again.
    ImGui::BeginDisabled(m_selected_path.empty());
    if (ImGui::Button("Reload"))
        reloadFlow();
    recordItemRect(m_button_rects.reload);
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("New from Circuit"))
        newFlowFromCircuit();
    recordItemRect(m_button_rects.new_from_circuit);

    ImGui::SameLine();
    ImGui::BeginDisabled(!state.loaded);
    if (ImGui::Button("Save Flow...")) {
        if (save_dialog)
            save_dialog();
    }
    recordItemRect(m_button_rects.save_flow);
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!state.runnable);
    if (ImGui::Button("Run"))
        run();
    recordItemRect(m_button_rects.run);
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!m_result || !m_result->ok || m_restore_failed);
    if (ImGui::Button("Export...")) {
        if (export_dialog)
            export_dialog();
    }
    recordItemRect(m_button_rects.export_button);
    ImGui::EndDisabled();

    if (state.loaded) {
        ImGui::SeparatorText("Conditions");
        if (state.conditions.empty()) {
            ImGui::TextUnformatted("None: a single row.");
        } else if (ImGui::BeginTable("conditions", 5,
                                     ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                         ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Component");
            ImGui::TableSetupColumn("Path");
            ImGui::TableSetupColumn("Values");
            ImGui::TableSetupColumn("Count");
            ImGui::TableSetupColumn("##actions");
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < state.conditions.size(); ++i) {
                const ConditionEntry &entry = state.conditions[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                if (entry.resolved)
                    ImGui::Text("%d (%s)", entry.component, entry.component_label.c_str());
                else
                    ImGui::TextColored(kIssueColor, "%d (unresolved)", entry.component);

                ImGui::TableNextColumn();
                // A key that is not a key of this engine's serialize() is the
                // likeliest hand-authoring mistake, so it is marked here and the
                // keys that do exist are listed below the table.
                if (entry.resolved && !entry.path_resolved)
                    ImGui::TextColored(kIssueColor, "%s", entry.path.c_str());
                else
                    ImGui::TextUnformatted(entry.path.c_str());

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(formatValueList(entry.values, entry.value_count).c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%zu", entry.value_count);

                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Edit"))
                    beginEditingCondition(i);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                    removeCondition(i);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        // The answer to a mistyped key: what this engine's serialize() actually
        // exposes, so the next attempt is an informed one rather than another guess.
        for (const ConditionEntry &entry : state.conditions) {
            if (!entry.resolved || entry.path_resolved)
                continue;
            std::string hint = "sweepable keys on " + std::to_string(entry.component) + " (" +
                               entry.component_label + "): ";
            if (entry.path_hints.empty()) {
                hint += "none";
            } else {
                for (size_t i = 0; i < entry.path_hints.size(); ++i) {
                    if (i > 0)
                        hint += ", ";
                    hint += entry.path_hints[i];
                }
            }
            ImGui::TextColored(kIssueColor, "%s", hint.c_str());
        }

        ImGui::SeparatorText("Measurements");
        if (ImGui::BeginTable("measurements", 5,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Component");
            ImGui::TableSetupColumn("Output Port");
            ImGui::TableSetupColumn("Metric");
            ImGui::TableSetupColumn("Status");
            ImGui::TableSetupColumn("##actions");
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < state.measurements.size(); ++i) {
                const MeasurementEntry &entry = state.measurements[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                if (entry.component_resolved)
                    ImGui::Text("%d (%s)", entry.component, entry.component_label.c_str());
                else
                    ImGui::TextColored(kIssueColor, "%d (unresolved)", entry.component);

                ImGui::TableNextColumn();
                if (entry.port_resolved)
                    ImGui::Text("%d", entry.port);
                else
                    ImGui::TextColored(kIssueColor, "%d (unresolved)", entry.port);

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(entry.metric.c_str());

                ImGui::TableNextColumn();
                if (entry.port_resolved)
                    ImGui::TextUnformatted("resolved");
                else
                    ImGui::TextColored(kIssueColor, "unresolved");

                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Remove"))
                    removeMeasurement(i);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::Text("Expected rows: %zu", state.expected_rows);
        for (const std::string &issue : state.issues)
            ImGui::TextColored(kIssueColor, "%s", issue.c_str());
    }

    drawAuthoringForms();

    if (m_result && m_result->ok) {
        ImGui::SeparatorText("Results");
        size_t condition_columns = 0;
        size_t metric_columns = 0;
        for (const FlowRow &row : m_result->rows) {
            condition_columns = std::max(condition_columns, row.conditions.size());
            metric_columns = std::max(metric_columns, row.metrics.size());
        }

        // One row per FlowRow, addressed by its index so every cell keeps a
        // stable ImGui id across frames. The table is height-bounded and its rows
        // are clipped: submitting every row of a 10000-row result each frame
        // costs tens of milliseconds and would auto-fit the window to the whole
        // result instead of scrolling inside it.
        const int columns = 1 + static_cast<int>(condition_columns + metric_columns);
        const float table_height = ImGui::GetTextLineHeightWithSpacing() * kResultTableRows;
        if (columns > 1 && ImGui::BeginTable("results", columns,
                                             ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                 ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                                                 ImGuiTableFlags_SizingFixedFit,
                                             ImVec2(0.0f, table_height))) {
            ImGui::TableSetupColumn("Row");
            std::string label;
            for (size_t i = 0; i < condition_columns; ++i) {
                label = "Condition " + std::to_string(i);
                ImGui::TableSetupColumn(label.c_str());
            }
            for (size_t i = 0; i < metric_columns; ++i) {
                label = "Metric " + std::to_string(i);
                ImGui::TableSetupColumn(label.c_str());
            }
            ImGui::TableHeadersRow();

            const int row_count = static_cast<int>(std::min<size_t>(
                m_result->rows.size(), static_cast<size_t>(std::numeric_limits<int>::max())));
            ImGuiListClipper clipper;
            clipper.Begin(row_count);
            while (clipper.Step()) {
                for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                    const size_t row_index = static_cast<size_t>(r);
                    const FlowRow &row = m_result->rows[row_index];
                    ImGui::PushID(r);
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", row_index);

                    for (size_t c = 0; c < condition_columns; ++c) {
                        ImGui::TableNextColumn();
                        ImGui::PushID(static_cast<int>(c));
                        if (c < row.conditions.size()) {
                            const ConditionValue &value = row.conditions[c];
                            ImGui::Text("%d:%s = %s", value.component, value.path.c_str(),
                                        formatValue(value.value).c_str());
                        } else {
                            ImGui::TextUnformatted("-");
                        }
                        ImGui::PopID();
                    }

                    for (size_t c = 0; c < metric_columns; ++c) {
                        ImGui::TableNextColumn();
                        ImGui::PushID(static_cast<int>(condition_columns + c));
                        if (c < row.metrics.size()) {
                            const MetricSample &sample = row.metrics[c];
                            ImGui::Text("%d:%d:%s = %s", sample.component, sample.port,
                                        sample.name.c_str(), formatMetricValue(sample).c_str());
                        } else {
                            ImGui::TextUnformatted("-");
                        }
                        ImGui::PopID();
                    }

                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
    }

    ImGui::End();
}

void TestFlowWidget::drawAuthoringForms() {
    ImGui::SeparatorText("Authoring");
    const std::vector<AuthoringComponent> components = authoringComponents();

    const auto label_for = [&components](int component) -> const AuthoringComponent * {
        for (const AuthoringComponent &entry : components) {
            if (entry.component == component)
                return &entry;
        }
        return nullptr;
    };
    const auto path_offer = [&](const AuthoringComponent *entry,
                                const std::string &path) -> const AuthoringPath * {
        if (!entry)
            return nullptr;
        for (const AuthoringPath &offer : entry->paths) {
            if (offer.path == path)
                return &offer;
        }
        return nullptr;
    };

    // ---- Condition form -----------------------------------------------------
    const AuthoringComponent *chosen = label_for(m_form_component);
    ImGui::SetNextItemWidth(240);
    if (ImGui::BeginCombo("Component", chosen ? chosen->label.c_str() : "<pick a component>")) {
        for (const AuthoringComponent &entry : components) {
            const bool selected = entry.component == m_form_component;
            if (ImGui::Selectable(entry.label.c_str(), selected))
                setAuthoringComponent(entry.component);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    // The key picker lists exactly the component's serialize() keys: numeric ones
    // are selectable and show the value a new sweep would start from, a key the
    // draft already sweeps is marked, and a non-numeric key is shown but disabled
    // with its JSON type as the reason. This is the picker that removes the
    // `atten_dB` / `attenuation_dB` class of mistake.
    const AuthoringPath *offer = path_offer(chosen, m_form_path);
    std::string key_label = "<pick a key>";
    if (offer)
        key_label = offer->path + " (" + offer->type_name + " = " + formatValue(offer->value) + ")";
    else if (!m_form_path.empty())
        key_label = m_form_path + " (not a key here)";
    ImGui::SetNextItemWidth(340);
    if (ImGui::BeginCombo("Key", key_label.c_str())) {
        if (!chosen) {
            ImGui::TextDisabled("Pick a component first");
        } else if (chosen->paths.empty()) {
            ImGui::TextDisabled("This component exposes no keys");
        }
        if (chosen) {
            for (const AuthoringPath &candidate : chosen->paths) {
                std::string label = candidate.path + "  (" + candidate.type_name + " = " +
                                    formatValue(candidate.value) + ")";
                if (candidate.used)
                    label += "  [already swept]";
                if (!candidate.numeric) {
                    ImGui::BeginDisabled();
                    ImGui::Selectable(label.c_str(), false);
                    ImGui::EndDisabled();
                    continue;
                }
                const bool selected = candidate.path == m_form_path;
                if (ImGui::Selectable(label.c_str(), selected))
                    setAuthoringPath(candidate.path);
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    // The values field is the model's own text, so the parse verdict and the
    // refusal a commit would give are visible while typing. The ImGui buffer is
    // sized from that text (with headroom for typing) rather than fixed: a fixed
    // one truncated a long sweep on the next keystroke, which stored the truncated
    // text back and silently dropped values from the list.
    if (m_values_buffer.size() < m_form_values.size() + kValuesBufferHeadroom)
        m_values_buffer.resize(m_form_values.size() + kValuesBufferHeadroom);
    std::snprintf(m_values_buffer.data(), m_values_buffer.size(), "%s", m_form_values.c_str());
    ImGui::SetNextItemWidth(420);
    if (ImGui::InputTextWithHint("Values", "e.g. -30, -20, -10", m_values_buffer.data(),
                                 m_values_buffer.size()))
        setAuthoringValuesText(m_values_buffer.data());
    if (!m_form_error.empty())
        ImGui::TextColored(kIssueColor, "%s", m_form_error.c_str());

    const bool editing = m_form_edit_index >= 0;
    ImGui::BeginDisabled(m_form_component < 0 || m_form_path.empty());
    if (ImGui::Button(editing ? "Update condition" : "Add condition"))
        commitAuthoringForm();
    recordItemRect(m_button_rects.add_condition);
    ImGui::EndDisabled();
    if (editing) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel edit"))
            cancelEditingCondition();
        recordItemRect(m_button_rects.cancel_edit);
        ImGui::SameLine();
        ImGui::Text("editing condition %d", m_form_edit_index + 1);
    }

    // ---- Measurement form ---------------------------------------------------
    ImGui::SeparatorText("Author a measurement");
    const AuthoringComponent *measured = label_for(m_form_meas_component);
    ImGui::SetNextItemWidth(240);
    if (ImGui::BeginCombo("Measure component",
                          measured ? measured->label.c_str() : "<pick a component>")) {
        for (const AuthoringComponent &entry : components) {
            IComponentEngine *component = componentById(entry.component);
            // Only something with an output port can be measured; listing the rest
            // would offer a reference the harness refuses.
            if (!component || component->node().outputs.empty()) {
                ImGui::BeginDisabled();
                ImGui::Selectable((entry.label + "  (no output port)").c_str(), false);
                ImGui::EndDisabled();
                continue;
            }
            const bool selected = entry.component == m_form_meas_component;
            if (ImGui::Selectable(entry.label.c_str(), selected))
                setAuthoringMeasurement(entry.component, 0, m_form_meas_metric);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    IComponentEngine *measured_component = componentById(m_form_meas_component);
    const size_t ports = measured_component ? measured_component->node().outputs.size() : 0;
    ImGui::SetNextItemWidth(80);
    if (ImGui::BeginCombo("Port", std::to_string(m_form_meas_port).c_str())) {
        for (size_t port = 0; port < ports; ++port) {
            const std::string label = std::to_string(port);
            if (ImGui::Selectable(label.c_str(), static_cast<size_t>(m_form_meas_port) == port))
                setAuthoringMeasurement(m_form_meas_component, static_cast<int>(port),
                                        m_form_meas_metric);
        }
        if (ports == 0)
            ImGui::TextDisabled("Pick a component first");
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    // The registry's names() is unordered; sorting gives the combo a stable order.
    std::vector<std::string> metrics = MetricRegistry::instance().names();
    std::sort(metrics.begin(), metrics.end());
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("Metric", m_form_meas_metric.c_str())) {
        for (const std::string &metric : metrics) {
            const bool selected = metric == m_form_meas_metric;
            if (ImGui::Selectable(metric.c_str(), selected))
                setAuthoringMeasurement(m_form_meas_component, m_form_meas_port, metric);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::BeginDisabled(m_form_meas_component < 0);
    if (ImGui::Button("Add measurement"))
        commitAuthoringMeasurement();
    ImGui::EndDisabled();
}

void TestFlowWidget::resetAfterCircuitReload() {
    m_restore_failed = false;
    m_restore_failure.clear();
    m_result.reset();
    m_status.clear();
    // The selection, its parsed spec and any in-tool draft are retained on
    // purpose: preview() revalidates them against the new circuit every frame, so
    // an id or port the replacement circuit no longer has keeps Run disabled
    // without discarding the user's work.
}
