#pragma once
#include "network_analyzer_engine.h"

class NodeGraphEngine;

class NetworkAnalyzerWidget {
  public:
    // engine: the singleton instrument engine (params + results); graph: used
    // to enumerate every real output pin for the Point A/B pickers.
    NetworkAnalyzerWidget(NetworkAnalyzerEngine &engine, NodeGraphEngine &graph);

    void draw(const char *title, bool *p_open = nullptr);

  private:
    NetworkAnalyzerEngine &m_engine;
    NodeGraphEngine &m_graph;
};
