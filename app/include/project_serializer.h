#pragma once

#include <array>
#include <string>
#include <utility>

class CircuitRuntime;
class GraphEditorActions;
class ComponentRegistry;
class NetworkAnalyzerEngine;
class NodeGraphEngine;
class NodeGraphWidget;
class PFBViewManager;
class SessionState;

// Owns the .rfsim JSON save/load/new logic previously inlined in
// RfSimulatorApp (issue #51: 1320-line god-object).
class ProjectSerializer {
  public:
    ProjectSerializer(CircuitRuntime &runtime, GraphEditorActions &editor_actions,
                      NodeGraphWidget &graph_widget, PFBViewManager &pfb_views, SessionState &state,
                      bool &show_log, bool &show_spectrum, bool &show_properties,
                      bool &show_node_editor, NetworkAnalyzerEngine &na_engine);

    bool save(const std::string &path); // false on open/write/flush/close failure (logged)
    bool load(const std::string &path); // false on parse/unknown-type failure (logged)
    // True when the latest load reset project state before returning.
    bool lastLoadReset() const { return m_last_load_reset; }
    void reset(); // newProject: links, components, probes, counters, PFBs

  private:
    // Canonical `window_state` scalar flags as (JSON key, live member). save()
    // writes them, load()'s shape guard validates them, and load()'s restore
    // reads them — all from this one list, so a new flag cannot be persisted
    // without also being shape-validated (issue #113).
    using WindowFlag = std::pair<const char *, bool *>;
    std::array<WindowFlag, 4> windowFlags();

    const ComponentRegistry &components() const;
    const NodeGraphEngine &graph() const;

    CircuitRuntime &m_runtime;
    GraphEditorActions &m_editor_actions;
    NodeGraphWidget &m_graph_widget;
    PFBViewManager &m_pfb_views;
    SessionState &m_state;
    bool &m_show_log;
    bool &m_show_spectrum;
    bool &m_show_properties;
    bool &m_show_node_editor;
    NetworkAnalyzerEngine &m_na_engine;
    bool m_last_load_reset = false;
};
