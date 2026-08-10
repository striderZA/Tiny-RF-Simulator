#pragma once

#include <string>

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
    ProjectSerializer(ComponentRegistry &components, NodeGraphEngine &graph,
                      NodeGraphWidget &graph_widget, PFBViewManager &pfb_views, SessionState &state,
                      int &next_component_id, bool &show_log, bool &show_spectrum,
                      bool &show_properties, bool &show_node_editor,
                      NetworkAnalyzerEngine &na_engine);

    void save(const std::string &path);
    bool load(const std::string &path); // false on parse/unknown-type failure (logged)
    void reset();                       // newProject: links, components, probes, counters, PFBs

  private:
    ComponentRegistry &m_components;
    NodeGraphEngine &m_graph;
    NodeGraphWidget &m_graph_widget;
    PFBViewManager &m_pfb_views;
    SessionState &m_state;
    int &m_next_component_id;
    bool &m_show_log;
    bool &m_show_spectrum;
    bool &m_show_properties;
    bool &m_show_node_editor;
    NetworkAnalyzerEngine &m_na_engine;
};
