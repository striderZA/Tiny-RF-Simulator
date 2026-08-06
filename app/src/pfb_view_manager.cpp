#include "pfb_view_manager.h"
#include "component_registry.h"
#include "session_state.h"

void PFBViewManager::addFor(PFBChannelizerEngine &engine, SessionState &state) {
    m_iq_widgets.push_back(std::make_unique<IQPlotWidget>(engine));
    m_show_iq_pfbs.push_back(
        state.loadBool("WindowState", ("IQPlot_" + std::to_string(engine.id())).c_str(), true));
    m_pfb_grid_widgets.push_back(std::make_unique<PFBChannelizerWidget>(engine));
    m_show_pfb_grids.push_back(
        state.loadBool("WindowState", ("PFBGrid_" + std::to_string(engine.id())).c_str(), true));
}

void PFBViewManager::rebuild(const ComponentRegistry &components, SessionState &state) {
    clear();
    for (auto *pfb : components.byType<PFBChannelizerEngine>())
        addFor(*pfb, state);
}

void PFBViewManager::clear() {
    m_iq_widgets.clear();
    m_show_iq_pfbs.clear();
    m_pfb_grid_widgets.clear();
    m_show_pfb_grids.clear();
}

void PFBViewManager::draw() {
    for (size_t i = 0; i < m_iq_widgets.size(); ++i) {
        if (m_show_iq_pfbs[i]) {
            std::string label = "IQ Plot - PFB " + std::to_string(i);
            bool show = m_show_iq_pfbs[i];
            m_iq_widgets[i]->draw(label.c_str(), &show);
            m_show_iq_pfbs[i] = show;
        }
    }
    for (size_t i = 0; i < m_pfb_grid_widgets.size(); ++i) {
        if (m_show_pfb_grids[i]) {
            std::string label = "Channelizer Grid - PFB " + std::to_string(i);
            bool show = m_show_pfb_grids[i];
            m_pfb_grid_widgets[i]->draw(label.c_str(), &show);
            m_show_pfb_grids[i] = show;
        }
    }
}

void PFBViewManager::saveVisibility(const ComponentRegistry &components,
                                    SessionState &state) const {
    auto pfb_vec = components.byType<PFBChannelizerEngine>();
    for (size_t i = 0; i < m_show_iq_pfbs.size() && i < pfb_vec.size(); ++i) {
        std::string key = "IQPlot_" + std::to_string(pfb_vec[i]->id());
        state.saveBool("WindowState", key.c_str(), m_show_iq_pfbs[i]);
    }
    for (size_t i = 0; i < m_show_pfb_grids.size() && i < pfb_vec.size(); ++i) {
        std::string key = "PFBGrid_" + std::to_string(pfb_vec[i]->id());
        state.saveBool("WindowState", key.c_str(), m_show_pfb_grids[i]);
    }
}
