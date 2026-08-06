#pragma once

#include "iq_plot_widget.h"
#include "pfb_channelizer_engine.h"
#include "pfb_channelizer_widget.h"
#include <memory>
#include <vector>

class ComponentRegistry;
class SessionState;

// Owns the per-PFB view widgets and their visibility flags. The app's old
// four lockstep vectors (m_iq_widgets/m_show_iq_pfbs/m_pfb_grid_widgets/
// m_show_pfb_grids) were rebuilt by hand at six call sites and caused issue
// #37 (use-after-free). All lifecycle now funnels through this class.
class PFBViewManager {
  public:
    void addFor(PFBChannelizerEngine &engine, SessionState &state);
    void rebuild(const ComponentRegistry &components, SessionState &state);
    void clear();
    void draw();
    void saveVisibility(const ComponentRegistry &components, SessionState &state) const;

    std::vector<bool> &iqVisibility() { return m_show_iq_pfbs; }
    std::vector<bool> &gridVisibility() { return m_show_pfb_grids; }

  private:
    std::vector<std::unique_ptr<IQPlotWidget>> m_iq_widgets;
    std::vector<bool> m_show_iq_pfbs;
    std::vector<std::unique_ptr<PFBChannelizerWidget>> m_pfb_grid_widgets;
    std::vector<bool> m_show_pfb_grids;
};
