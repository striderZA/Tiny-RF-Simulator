#include "network_analyzer_view_manager.h"
#include "component_registry.h"
#include "session_state.h"

void NetworkAnalyzerViewManager::addFor(NetworkAnalyzerEngine &engine, SessionState &state) {
    m_engines.push_back(&engine);
    m_widgets.push_back(std::make_unique<NetworkAnalyzerWidget>(engine));
    m_show.push_back(state.loadBool(
        "WindowState", ("NetworkAnalyzer_" + std::to_string(engine.id())).c_str(), true));
}

void NetworkAnalyzerViewManager::rebuild(const ComponentRegistry &components, SessionState &state) {
    clear();
    for (auto *na : components.byType<NetworkAnalyzerEngine>())
        addFor(*na, state);
}

void NetworkAnalyzerViewManager::clear() {
    m_engines.clear();
    m_widgets.clear();
    m_show.clear();
}

void NetworkAnalyzerViewManager::draw() {
    for (size_t i = 0; i < m_widgets.size(); ++i) {
        if (m_show[i]) {
            std::string label = "Network Analyzer " + std::to_string(m_engines[i]->id());
            bool show = m_show[i];
            m_widgets[i]->draw(label.c_str(), &show);
            m_show[i] = show;
        }
    }
}

void NetworkAnalyzerViewManager::saveVisibility(const ComponentRegistry &components,
                                                SessionState &state) const {
    auto na_vec = components.byType<NetworkAnalyzerEngine>();
    for (size_t i = 0; i < m_show.size() && i < na_vec.size(); ++i) {
        std::string key = "NetworkAnalyzer_" + std::to_string(na_vec[i]->id());
        state.saveBool("WindowState", key.c_str(), m_show[i]);
    }
}
