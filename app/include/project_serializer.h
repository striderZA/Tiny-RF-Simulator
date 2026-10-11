#pragma once

#include "receiver_requirements.h"
#include <array>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>

class CircuitRuntime;
class GraphEditorActions;
class ComponentRegistry;
class NetworkAnalyzerEngine;
class NodeGraphEngine;
class NodeGraphWidget;
class PFBViewManager;

struct ProjectJsonOptions {
    std::optional<std::filesystem::path> sparam_root;
    bool window_state = true;
};

// Owns the .rfsim JSON save/load/new logic previously inlined in
// RfSimulatorApp (issue #51: 1320-line god-object).
class ProjectSerializer {
  public:
    ProjectSerializer(CircuitRuntime &runtime, GraphEditorActions &editor_actions,
                      NodeGraphWidget &graph_widget, PFBViewManager &pfb_views,
                      ReceiverRequirementsState &receiver_requirements, bool &show_log,
                      bool &show_spectrum, bool &show_properties, bool &show_node_editor,
                      NetworkAnalyzerEngine &na_engine);

    // Build or restore project state without file I/O. Without sparam_root,
    // paths remain verbatim; window_state controls UI-flag writing/restoration.
    nlohmann::json toJson(const ProjectJsonOptions &options);
    bool fromJson(nlohmann::json root, const ProjectJsonOptions &options,
                  const std::string &source_label);

    bool save(const std::string &path); // false on open/write/flush/close failure (logged)
    bool load(const std::string &path); // false on file/parse/restoration failure (logged)
    // True when the latest restoration reset project state before returning.
    bool lastLoadReset() const { return m_last_load_reset; }
    // newProject: links, components, probes, counters. Clears PFB views because
    // it destroys every engine; the app re-syncs component-bound views after
    // reset() and every fromJson() restoration.
    void reset();

  private:
    // Canonical `window_state` scalar flags as (JSON key, live member). toJson()
    // writes them; fromJson() validates enabled flags and restores them from
    // this one list (issue #113).
    using WindowFlag = std::pair<const char *, bool *>;
    std::array<WindowFlag, 4> windowFlags();

    const ComponentRegistry &components() const;
    const NodeGraphEngine &graph() const;

    CircuitRuntime &m_runtime;
    GraphEditorActions &m_editor_actions;
    NodeGraphWidget &m_graph_widget;
    PFBViewManager &m_pfb_views;
    ReceiverRequirementsState &m_receiver_requirements;
    bool &m_show_log;
    bool &m_show_spectrum;
    bool &m_show_properties;
    bool &m_show_node_editor;
    NetworkAnalyzerEngine &m_na_engine;
    bool m_last_load_reset = false;
};
