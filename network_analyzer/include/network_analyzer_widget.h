#pragma once
#include "network_analyzer_engine.h"

class NetworkAnalyzerWidget {
  public:
    explicit NetworkAnalyzerWidget(NetworkAnalyzerEngine &engine) : m_engine(engine) {}

    void draw(const char *title, bool *p_open = nullptr);

  private:
    NetworkAnalyzerEngine &m_engine;
};
