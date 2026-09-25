#include "test_flow_widget.h"

#include "component_interface.h"
#include "component_registry.h"
#include "imgui.h"
#include "node_graph_engine.h"
#include "rewire.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

const ImVec4 kIssueColor(1.0f, 0.45f, 0.45f, 1.0f);

void appendError(std::string &out, const std::string &detail) {
    if (!out.empty())
        out += "; ";
    out += detail;
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
    : m_components(components), m_graph(graph) {}

bool TestFlowWidget::loadFlow(const std::string &path) {
    // The new selection replaces the old one outright: a parse failure must
    // never leave the previous flow's rows on screen next to its error.
    m_selected_path = path;
    m_result.reset();
    m_status.clear();
    m_load_state = LoadFlowFile(path);
    if (!m_load_state.ok)
        m_status = m_load_state.error.message;
    // A latched restoration failure outlives the selection: whichever file the
    // user picks next — valid or not — the panel keeps stating why nothing may
    // run until the circuit is reloaded.
    if (m_restore_failed) {
        if (!m_status.empty())
            m_status += '\n';
        m_status += m_restore_failure;
    }
    return m_load_state.ok;
}

bool TestFlowWidget::run() {
    if (m_restore_failed) {
        // Re-state the original failure (which names the reload) rather than a
        // generic line: the latch, not this call, is why the run is refused.
        m_status = m_restore_failure;
        return false;
    }
    if (!m_load_state.ok) {
        m_status = "Load a valid flow before running.";
        return false;
    }
    // A sweep above the panel's budget would block the UI thread for minutes and
    // build a row vector the panel cannot render, so it is refused here rather
    // than discovered mid-run. preview() reports the same limit, so the disabled
    // Run button always has a reason on screen.
    if (expectedRowCount(m_load_state.spec) > kMaxRunRows) {
        m_status = "Run refused: the sweep exceeds the panel's " + std::to_string(kMaxRunRows) +
                   "-row limit.";
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
        m_status = std::string("Run refused: cannot snapshot the circuit (") + error.what() + ")";
        return false;
    } catch (...) {
        m_result.reset();
        m_status = "Run refused: cannot snapshot the circuit (unknown exception)";
        return false;
    }

    // 2. Execute. From here the circuit has been touched whatever happens, so
    //    the restore below is unconditional.
    FlowResult result;
    std::string execution_error;
    try {
        result = RunFlow(m_load_state.spec, m_components.all(), m_graph);
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
        m_status = "Execution failed: " + execution_error;
        return false;
    }
    m_result = result;
    if (!result.ok) {
        m_status = result.error.message;
        return false;
    }
    m_status = "Flow completed: " + std::to_string(result.rows.size()) + " row(s).";
    return true;
}

bool TestFlowWidget::exportResult(const std::string &path) {
    // The result stays in memory whatever the write does: a failed export is a
    // report, not a discard, so the user can pick another path and retry.
    if (m_restore_failed || !m_result || !m_result->ok) {
        m_status = "Export unavailable: no successful run to export.";
        if (m_restore_failed)
            m_status += "\n" + m_restore_failure;
        return false;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        m_status = "Export failed: cannot write '" + path + "'.";
        return false;
    }
    out << m_result->toJson().dump(2) << '\n';
    out.flush();
    if (!out) {
        out.close();
        m_status = "Export failed: write error on '" + path + "'.";
        return false;
    }
    out.close();
    if (!out.good()) {
        m_status = "Export failed: close error on '" + path + "'.";
        return false;
    }
    m_status = "Exported results to '" + path + "'.";
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

TestFlowWidget::FlowPreview TestFlowWidget::preview() const {
    FlowPreview out;
    out.loaded = m_load_state.ok;
    out.name = m_load_state.spec.name;
    if (!out.loaded)
        return out;

    const std::span<IComponentEngine *const> components = m_components.all();

    for (const Condition &condition : m_load_state.spec.conditions) {
        ConditionEntry entry;
        entry.component = condition.component;
        entry.path = condition.path;
        entry.value_count = condition.values.size();
        const size_t shown = std::min(entry.value_count, kPreviewValues);
        entry.values.assign(condition.values.begin(), condition.values.begin() + shown);
        if (IComponentEngine *component = findById(components, condition.component)) {
            entry.resolved = true;
            entry.component_label = std::string(component->type_name());
        }
        out.conditions.push_back(std::move(entry));
    }

    for (const Measurement &measurement : m_load_state.spec.measure) {
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
    for (const FlowError &error : ValidateFlow(m_load_state.spec, components))
        out.issues.push_back(error.message);

    out.expected_rows = expectedRowCount(m_load_state.spec);
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
                          const std::function<void()> &export_dialog) {
    if (open && !*open)
        return;
    if (!ImGui::Begin(title, open)) {
        ImGui::End();
        return;
    }
    m_button_rects = ButtonRects{};

    if (m_selected_path.empty())
        ImGui::TextUnformatted("No flow selected.");
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
        } else if (ImGui::BeginTable("conditions", 4,
                                     ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                         ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Component");
            ImGui::TableSetupColumn("Path");
            ImGui::TableSetupColumn("Values");
            ImGui::TableSetupColumn("Count");
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
                ImGui::TextUnformatted(entry.path.c_str());

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(formatValueList(entry.values, entry.value_count).c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%zu", entry.value_count);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Measurements");
        if (ImGui::BeginTable("measurements", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Component");
            ImGui::TableSetupColumn("Output Port");
            ImGui::TableSetupColumn("Metric");
            ImGui::TableSetupColumn("Status");
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
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::Text("Expected rows: %zu", state.expected_rows);
        for (const std::string &issue : state.issues)
            ImGui::TextColored(kIssueColor, "%s", issue.c_str());
    }

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

void TestFlowWidget::resetAfterCircuitReload() {
    m_restore_failed = false;
    m_restore_failure.clear();
    m_result.reset();
    m_status.clear();
    // The selection and its parsed spec are retained on purpose: preview()
    // revalidates them against the new circuit every frame, so an id or port the
    // replacement circuit no longer has keeps Run disabled without discarding
    // the user's choice.
}
