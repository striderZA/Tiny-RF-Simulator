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
// #37 (use-after-free). All lifecycle now funnels through this class, and the
// views are derived from the registry by sync() rather than by per-type
// branches at each add/duplicate/load/remove site.
class PFBViewManager {
  public:
    // Reconciles the views with the registry's PFBs, in registry order (the
    // order saveVisibility() pairs flags with): views of surviving PFBs keep
    // their widgets and in-session visibility, new PFBs get views with their
    // persisted visibility, and views of removed PFBs are destroyed. Must run
    // after every component-set change, before views are drawn again.
    void sync(const ComponentRegistry &components, SessionState &state);
    void clear();
    void draw();
    void saveVisibility(const ComponentRegistry &components, SessionState &state) const;

    std::size_t size() const { return m_engines.size(); }
    std::vector<bool> &iqVisibility() { return m_show_iq_pfbs; }
    std::vector<bool> &gridVisibility() { return m_show_pfb_grids; }

  private:
    // Identity of each view's engine. Compared by address only, never
    // dereferenced, so a removed engine's stale entry is safe to match against.
    std::vector<const PFBChannelizerEngine *> m_engines;
    std::vector<std::unique_ptr<IQPlotWidget>> m_iq_widgets;
    std::vector<bool> m_show_iq_pfbs;
    std::vector<std::unique_ptr<PFBChannelizerWidget>> m_pfb_grid_widgets;
    std::vector<bool> m_show_pfb_grids;
};
