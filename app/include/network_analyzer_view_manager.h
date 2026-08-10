#pragma once
#include "network_analyzer_engine.h"
#include "network_analyzer_widget.h"
#include <memory>
#include <vector>

class ComponentRegistry;
class SessionState;

// Owns the per-NetworkAnalyzer result-plot widgets and their visibility
// flags. Mirrors PFBViewManager's shape (app/include/pfb_view_manager.h);
// kept as a separate class since PFBViewManager is explicitly PFB-scoped.
class NetworkAnalyzerViewManager {
  public:
    void addFor(NetworkAnalyzerEngine &engine, SessionState &state);
    void rebuild(const ComponentRegistry &components, SessionState &state);
    void clear();
    void draw();
    void saveVisibility(const ComponentRegistry &components, SessionState &state) const;

    std::vector<bool> &visibility() { return m_show; }
    const std::vector<NetworkAnalyzerEngine *> &engines() const { return m_engines; }

  private:
    std::vector<NetworkAnalyzerEngine *> m_engines;
    std::vector<std::unique_ptr<NetworkAnalyzerWidget>> m_widgets;
    std::vector<bool> m_show;
};
