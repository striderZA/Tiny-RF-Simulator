#include "pfb_view_manager.h"
#include "component_registry.h"
#include "session_state.h"
#include <algorithm>
#include <iterator>

void PFBViewManager::sync(const ComponentRegistry &components, SessionState &state) {
    std::vector<int> node_ids;
    std::vector<std::unique_ptr<IQPlotWidget>> iq_widgets;
    std::vector<bool> show_iq;
    std::vector<std::unique_ptr<PFBChannelizerWidget>> grid_widgets;
    std::vector<bool> show_grid;

    for (auto *pfb : components.byType<PFBChannelizerEngine>()) {
        node_ids.push_back(pfb->graphNodeId());
        const auto existing = std::find(m_node_ids.begin(), m_node_ids.end(), pfb->graphNodeId());
        if (existing != m_node_ids.end()) {
            const auto i = static_cast<std::size_t>(std::distance(m_node_ids.begin(), existing));
            iq_widgets.push_back(std::move(m_iq_widgets[i]));
            show_iq.push_back(m_show_iq_pfbs[i]);
            grid_widgets.push_back(std::move(m_pfb_grid_widgets[i]));
            show_grid.push_back(m_show_pfb_grids[i]);
            continue;
        }
        const std::string id = std::to_string(pfb->id());
        iq_widgets.push_back(std::make_unique<IQPlotWidget>(*pfb));
        show_iq.push_back(state.loadBool("WindowState", ("IQPlot_" + id).c_str(), true));
        grid_widgets.push_back(std::make_unique<PFBChannelizerWidget>(*pfb));
        show_grid.push_back(state.loadBool("WindowState", ("PFBGrid_" + id).c_str(), true));
    }

    // Widgets not moved above belong to removed PFBs and are destroyed here.
    m_node_ids = std::move(node_ids);
    m_iq_widgets = std::move(iq_widgets);
    m_show_iq_pfbs = std::move(show_iq);
    m_pfb_grid_widgets = std::move(grid_widgets);
    m_show_pfb_grids = std::move(show_grid);
}

void PFBViewManager::clear() {
    m_node_ids.clear();
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
