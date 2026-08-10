#pragma once
#include "network_analyzer_engine.h"
#include <functional>

class NodeGraphEngine;

class NetworkAnalyzerWidget {
  public:
    // engine: the singleton instrument engine (params + results); graph: used
    // to enumerate every real output pin for the Point A/B pickers.
    NetworkAnalyzerWidget(NetworkAnalyzerEngine &engine, NodeGraphEngine &graph);

    // Fired once at the end of a draw() frame in which the user changed any
    // sweep parameter or the Point A/B pickers. Wired to
    // RfSimulatorApp::markDirty() (mirrors InspectorPanel::onParamChange).
    std::function<void()> onParamChange;

    void draw(const char *title, bool *p_open = nullptr);

  private:
    NetworkAnalyzerEngine &m_engine;
    NodeGraphEngine &m_graph;
    bool m_param_edited = false;
};
