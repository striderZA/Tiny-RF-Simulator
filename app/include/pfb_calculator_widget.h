#pragma once

#include <functional>
#include <vector>

class NodeGraphEngine;
class ComponentRegistry;
class PFBChannelizerEngine;
class PfbFilterDesign;
struct ImDrawList;
struct ImVec2;

// Dockable PFB filter calculator: shows the achieved prototype metrics for
// M/K/beta against a rejection target, plots the response, and applies M/K/beta
// to a targeted PFB via the engine's existing setters. Pure DSP lives in the
// pfb_channelizer core (PfbFilterDesign); this widget only binds/renders.
class PfbCalculatorWidget {
  public:
    PfbCalculatorWidget(NodeGraphEngine &graph, ComponentRegistry &components);
    void draw(const char *title, bool *p_open = nullptr);
    std::function<void()> onParamChange; // fired after Apply writes engine params

  private:
    PFBChannelizerEngine *resolveTarget(const std::vector<PFBChannelizerEngine *> &pfbs);
    void pullFrom(PFBChannelizerEngine &pfb);
    void drawPlot(ImDrawList *dl, const ImVec2 &origin, float w, float h,
                  const PfbFilterDesign &design);

    NodeGraphEngine *m_graph;
    ComponentRegistry *m_components;
    int m_M = 32;
    int m_K = 8;
    float m_beta = 8.0f; // float for ImGui::SliderFloat
    float m_target_db = 80.0f;
    int m_target_index = -1; // -1 = auto: follow a single graph-selected PFB
    int m_bound_pfb_id = -1; // graph node id the controls were pulled from
};
