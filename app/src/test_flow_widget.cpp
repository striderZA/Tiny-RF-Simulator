#include "test_flow_widget.h"

#include "component_interface.h"
#include "component_registry.h"
#include "imgui.h"
#include "node_graph_engine.h"
#include "rewire.h"

#include <exception>
#include <fstream>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

IComponentEngine *findById(std::span<IComponentEngine *const> components, int id) {
    for (IComponentEngine *component : components) {
        if (component && component->id() == id)
            return component;
    }
    return nullptr;
}

void appendError(std::string &out, const std::string &detail) {
    if (!out.empty())
        out += "; ";
    out += detail;
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
    return m_load_state.ok;
}

bool TestFlowWidget::run() {
    if (m_restore_failed) {
        m_status = "Restoration failed; reload the circuit before running another flow.";
        return false;
    }
    if (!m_load_state.ok) {
        m_status = "Load a valid flow before running.";
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
        m_restore_failed = true;
        m_result.reset();
        m_status = "Restore failed: " + restore_error + " — reload the circuit to run flows again.";
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

bool TestFlowWidget::exportResult(const std::string &path) const {
    if (m_restore_failed || !m_result || !m_result->ok)
        return false;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;
    out << m_result->toJson().dump(2) << '\n';
    out.flush();
    if (!out) {
        out.close();
        return false;
    }
    out.close();
    return out.good();
}

void TestFlowWidget::draw(const char *title, bool *open, const std::function<void()> &open_dialog,
                          const std::function<void()> &export_dialog) {
    if (open && !*open)
        return;
    if (!ImGui::Begin(title, open)) {
        ImGui::End();
        return;
    }

    if (m_selected_path.empty())
        ImGui::TextUnformatted("No flow selected.");
    else
        ImGui::TextWrapped("Flow: %s", m_selected_path.c_str());
    if (!m_status.empty())
        ImGui::TextWrapped("%s", m_status.c_str());

    if (ImGui::Button("Open Flow...") && open_dialog)
        open_dialog();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_load_state.ok || m_restore_failed);
    if (ImGui::Button("Run"))
        run();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_result || !m_result->ok || m_restore_failed);
    if (ImGui::Button("Export...") && export_dialog)
        export_dialog();
    ImGui::EndDisabled();

    ImGui::End();
}

void TestFlowWidget::resetAfterCircuitReload() {
    m_restore_failed = false;
    m_result.reset();
    m_status.clear();
}
