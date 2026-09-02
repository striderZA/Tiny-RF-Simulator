#pragma once

#include <functional>
#include <vector>

#include "pfb_filter_design.h"

class ComponentRegistry;
class PFBChannelizerEngine;
struct ImDrawList;
struct ImVec2;

// Dockable PFB filter calculator: shows the achieved prototype metrics for
// M/K/beta against a rejection target, plots the response, and applies M/K/beta
// to a targeted PFB via the engine's existing setters. Pure DSP lives in the
// pfb_channelizer core (PfbFilterDesign); this widget only binds/renders.
class PfbCalculatorWidget {
  public:
    PfbCalculatorWidget(ComponentRegistry &components);
    void draw(const char *title, bool *p_open = nullptr);
    std::function<void()> onParamChange; // fired after Apply writes engine params

  private:
    PFBChannelizerEngine *resolveTarget(const std::vector<PFBChannelizerEngine *> &pfbs);
    void pullFrom(PFBChannelizerEngine &pfb);
    void refreshDesignCache();
    void drawPlot(ImDrawList *dl, const ImVec2 &origin, float w, float h,
                  const std::vector<float> &db, double y_min, double y_max);

    ComponentRegistry *m_components;
    int m_M = 32;
    int m_K = 8;
    float m_beta = 8.0f; // float for ImGui::SliderFloat
    float m_target_db = 80.0f;
    int m_target_index = -1; // -1 = auto: follow a single graph-selected PFB
    int m_bound_pfb_id = -1; // graph node id the controls were pulled from
    PfbFilterDesign m_cached_design;
    PfbFilterMetrics m_cached_metrics;
    int m_cached_M = 0;
    int m_cached_K = 0;
    double m_cached_beta = -1.0;
    std::vector<float> m_plot_db; // 151 samples over x in [0, 1.5]
    double m_plot_ymin = 0.0;
    double m_plot_ymax = 0.0;
};
